// Lånemodellen för Stone (docs/stone_model.md), steg 1: en funktion i taget.
//
// Platser: en rot (lokal variabel, det en parameter refererar till, *this,
// en temporär) och en väg av fält. Ett steg nullptr är ägd lagring (element i
// en behållare, alternativet i en variant, det en referensmedlem pekar på):
// den kan försvinna när ägaren ändras. Fält ligger inline och överlever.
//
// Ett lån är en plats. Lånebärande värden (referenser, pekare, iteratorer,
// span, lambdor med referensfångst, structar som innehåller sådana) håller
// lån; för lokala variabler följs vilka lån de håller genom CFG:n.
//
// Kontrollerna:
//  G2  en ändring av plats P ogiltigförklarar lån som ligger i ägd lagring
//      under P. Lever en variabel som håller ett sådant lån (clangs
//      LiveVariables) är det ett fel. Samma när P dör (scope, temporär).
//  G3  två referensoperander till samma anrop överlappar inte om en av dem
//      skrivs.
//  En funktion returnerar eller lagrar aldrig ett lån av något lokalt.
//
// Effekter av anrop kommer från sammanfattningar (summaryOf). Utan
// sammanfattning: icke-const metod skriver objektet, T&-argument skrivs,
// lånebärande argument kan skriva genom sina lån, resultatet lånar ägd
// lagring i alla referensoperander. Det är sunt men oprecist; precision
// kommer från sammanfattningar, inte från fler regler här.

#include "LoanCheck.hpp"

#include "EnumSwitch.hpp"
#include "HintAttributes.hpp"
#include "LocalCheck.hpp"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/StmtCXX.h>
#include <clang/Analysis/Analyses/LiveVariables.h>
#include <clang/Analysis/Analyses/PostOrderCFGView.h>
#include <clang/Analysis/AnalysisDeclContext.h>
#include <clang/Analysis/CFG.h>
#include <clang/Basic/SourceManager.h>

#include <map>
#include <set>
#include <string>
#include <vector>

using namespace clang;

namespace pork {

namespace {

// --- Platser och lån ---------------------------------------------------------

struct Root {
  // object: lokal variabel/värdeparameter (decl); referent: det en parameter
  // refererar/pekar till (decl); self: *this; temporary: dör vid
  // fullständiga uttryckets slut (stmt); extended: livstidsförlängd temporär
  // (stmt); staticStorage: global/statisk/literal (decl eller stmt).
// referent utan decl: något utanför funktionen, okänt vad (det en pekare,
// iterator eller lambda i ett yttre objekt pekar på).
  enum Kind : unsigned char { object, referent, self, temporary, extended, staticStorage };
  Kind kind = object;
  const Decl* decl = nullptr;
  const Stmt* stmt = nullptr;
  auto operator<=>(const Root&) const = default;
};

Root rootOf(const Decl* d, Root::Kind k) { return {k, d, nullptr}; }
Root rootOf(const Stmt* st, Root::Kind k) { return {k, nullptr, st}; }
const Root kOutside{Root::referent, nullptr, nullptr};


struct Place {
  Root root;
  std::vector<const FieldDecl*> path;  // nullptr = ägd lagring / okänt delobjekt
  // Nivåer ägd lagring som slogs ihop med det sista steget (elementen i m[0]
  // i en vector<vector<int>> är m[*] med extra 1; vägarna växer inte i
  // loopar). Då är behållaren okänd: m[0].begin() och m[1].end() är båda
  // m[*]. Mättas vid kMaxExtra (okänt hur många).
  unsigned char extra = 0;
  // Nådd genom referensvariabeln `via`, bunden till platsen med väglängd
  // viaDepth och extra viaExtra. En referens binds inte om: allt som nås
  // genom samma bindning utgår från samma objekt (row.begin(), row.end() i
  // `for (auto& row : m)`). Tas bort när variabeln binds på nytt.
  const VarDecl* via = nullptr;
  unsigned char viaDepth = 0;
  unsigned char viaExtra = 0;
  auto operator<=>(const Place&) const = default;
};

// Den lokala variabeln som är roten, om någon.
const VarDecl* rootVar(const Place& p) {
  return p.root.kind == Root::object ? dyn_cast_or_null<VarDecl>(p.root.decl) : nullptr;
}

bool overlap(const Place& a, const Place& b) {
  if (a.root != b.root) return false;
  std::size_t n = std::min(a.path.size(), b.path.size());
  for (std::size_t i = 0; i < n; ++i)
    if (a.path[i] != nullptr && b.path[i] != nullptr && a.path[i] != b.path[i]) return false;
  return true;
}

// G2: en ändring av `changed` gör lånet av `loan` ogiltigt om lånet ligger i
// ägd lagring under den ändrade platsen.
bool invalidates(const Place& changed, const Place& loan) {
  if (!overlap(changed, loan) || loan.path.size() <= changed.path.size()) return false;
  for (std::size_t i = changed.path.size(); i < loan.path.size(); ++i)
    if (loan.path[i] == nullptr) return true;
  return false;
}

// Platsen dör: alla lån av den eller av något i den.
bool ends(const Place& dead, const Place& loan) {
  return overlap(dead, loan) && loan.path.size() >= dead.path.size();
}

// `nested`: p är ett objekt (en behållare) och resultatet dess element. Är p
// redan ägd lagring (m[0] i en vector<vector<int>>) är det en ny nivå som
// slås ihop (merged). Annars pekar p redan in i lagringen (en iterator, en
// span, en pekare) och resultatet är samma nivå (*it, it + 1, s.begin()).
constexpr unsigned char kMaxExtra = 3;

Place opened(Place p, bool nested = true) {
  if (p.path.empty() || p.path.back() != nullptr) p.path.push_back(nullptr);
  else if (nested && p.extra < kMaxExtra) ++p.extra;
  return p;
}

struct Loan {
  Place place;
  bool exclusive = false;
};

// Ett ställe som ett uttryck betecknar eller pekar på. `loan` >= 0 när det
// kommer från ett lån som en variabel redan håller (en kopia behåller lånet).
struct PlaceRef {
  Place place;
  bool exclusive = false;
  int loan = -1;
};

// --- Typer -------------------------------------------------------------------

const CXXRecordDecl* patternOf(const CXXRecordDecl* rd) {
  if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return s->getSpecializedTemplate()->getTemplatedDecl();
  return rd;
}

bool isGslOwner(const CXXRecordDecl* rd) { return rd->hasAttr<OwnerAttr>() || patternOf(rd)->hasAttr<OwnerAttr>(); }
bool isGslPointer(const CXXRecordDecl* rd) {
  return rd->hasAttr<PointerAttr>() || patternOf(rd)->hasAttr<PointerAttr>();
}

bool inStd(const Decl& d) {
  for (const DeclContext* dc = d.getDeclContext(); dc != nullptr; dc = dc->getParent())
    if (const auto* ns = dyn_cast<NamespaceDecl>(dc); ns != nullptr && ns->getParent()->isTranslationUnit())
      return ns->getName() == "std" || ns->getName() == "__gnu_cxx";
  return false;
}

class Types {
 public:
  // Bär ett värde av typen lån? Referenser hanteras för sig (bindning).
  // Kan ett objekt av typen `outer` innehålla ett objekt av typen `inner`
  // (samma typ, fält, baser, element i en ägande behållare)?
  bool reaches(QualType outer, QualType inner) {
    outer = outer.getCanonicalType().getUnqualifiedType();
    inner = inner.getCanonicalType().getUnqualifiedType();
    if (outer == inner) return true;
    const CXXRecordDecl* rd = outer->getAsCXXRecordDecl();
    if (const auto* a = outer->getAsArrayTypeUnsafe()) return reaches(a->getElementType(), inner);
    if (rd == nullptr || !rd->hasDefinition()) return false;
    rd = rd->getDefinition();
    auto key = std::make_pair(rd, inner.getAsOpaquePtr());
    if (auto it = reach_.find(key); it != reach_.end()) return it->second;
    reach_[key] = false;
    bool result = false;
    if (const auto* sp = dyn_cast<ClassTemplateSpecializationDecl>(rd); sp != nullptr && isGslOwner(rd))
      for (const TemplateArgument& a : sp->getTemplateArgs().asArray())
        result = result || (a.getKind() == TemplateArgument::Type && reaches(a.getAsType(), inner));
    for (const CXXBaseSpecifier& b : rd->bases()) result = result || reaches(b.getType(), inner);
    for (const FieldDecl* f : rd->fields())
      result = result || (!f->getType()->isReferenceType() && reaches(f->getType(), inner));
    reach_[key] = result;
    return result;
  }

  static bool initializerList(QualType t) {
    const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
    return spec != nullptr && spec->getSpecializedTemplate()->getQualifiedNameAsString() == "std::initializer_list";
  }

  bool loanBearing(QualType t) {
    t = t.getCanonicalType().getUnqualifiedType();
    if (t->isReferenceType()) return true;
    if (t->isPointerType() || t->isBlockPointerType()) return true;
    if (const auto* a = t->getAsArrayTypeUnsafe()) return loanBearing(a->getElementType());
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition()) return false;
    rd = rd->getDefinition();
    if (auto it = memo_.find(rd); it != memo_.end()) return it->second;
    memo_[rd] = false;  // cykel: en struct kan inte innehålla sig själv
    bool result = false;
    if (isGslPointer(rd)) {
      result = true;
    } else if (isGslOwner(rd)) {
      // En behållare äger sina element; den bär lån om elementen gör det.
      if (const auto* s = dyn_cast<ClassTemplateSpecializationDecl>(rd))
        for (const TemplateArgument& a : s->getTemplateArgs().asArray())
          result = result || (a.getKind() == TemplateArgument::Type && loanBearing(a.getAsType()));
    } else {
      for (const CXXBaseSpecifier& b : rd->bases()) result = result || loanBearing(b.getType());
      for (const FieldDecl* f : rd->fields()) result = result || loanBearing(f->getType());
    }
    memo_[rd] = result;
    return result;
  }

 private:
  std::map<const CXXRecordDecl*, bool> memo_;
  std::map<std::pair<const CXXRecordDecl*, const void*>, bool> reach_;
};

// --- Sammanfattningar av std (data: summaries/std.txt) ---------------------
//
// Effekten av ett anrop slås upp i LoanSummaries (core/LoanSummaries.hpp):
// kategori efter clangs gsl-märkning, namn, parametrarnas form. Ingen regel
// ger LoanEffect::standard, den försiktiga standarden (se filhuvudet).
using Effect = LoanEffect;

bool comparison(OverloadedOperatorKind k) {
  switch (k) {
    case OO_EqualEqual: case OO_ExclaimEqual: case OO_Less: case OO_Greater:
    case OO_LessEqual: case OO_GreaterEqual: case OO_Spaceship:
      return true;
    default:
      return false;
  }
}

// En niebloid i std::ranges (`std::ranges::find_if` är ett objekt av typen
// `__find_if_fn`): algoritmens namn, annars tomt.
std::string niebloid(const FunctionDecl& fd) {
  const auto* md = dyn_cast<CXXMethodDecl>(&fd);
  if (md == nullptr || md->getOverloadedOperator() != OO_Call || !inStd(*md)) return {};
  llvm::StringRef name = md->getParent()->getName();
  if (!name.consume_front("__") || !name.consume_back("_fn")) return {};
  return name.str();
}

// En iterator: en std-typ märkt [[gsl::Pointer]], eller en pekare
// (std::array::iterator, int*). Väljer överlagring: move(first, last, d).
bool iteratorType(QualType t) {
  QualType n = t.getNonReferenceType();
  if (n->isPointerType()) return true;
  const CXXRecordDecl* rd = n->getAsCXXRecordDecl();
  return rd != nullptr && isGslPointer(rd);
}

LoanRule libraryRuleImpl(const LoanSummaries& lib, const FunctionDecl* fd) {
  if (fd == nullptr || !inStd(*fd)) return {};
  LoanSummaries::Query q{LoanSummaries::Category::free, {}, {}, {}, false};
  for (const ParmVarDecl* p : fd->parameters()) q.iteratorParams.push_back(iteratorType(p->getType()));
  if (std::string n = niebloid(*fd); !n.empty()) {
    q.name = n;
  } else if (const auto* md = dyn_cast<CXXMethodDecl>(fd); md != nullptr && md->isInstance()) {
    const CXXRecordDecl* rd = md->getParent();
    if (isGslPointer(rd)) q.category = LoanSummaries::Category::pointer;
    else if (isGslOwner(rd)) q.category = LoanSummaries::Category::owner;
    else return {};
    q.name = isa<CXXConversionDecl>(md) ? "conversion" : md->getNameAsString();
    q.qualified = trustName(*md);
    q.isConst = md->isConst();
  } else {
    q.name = fd->getNameAsString();
    q.qualified = trustName(*fd);
  }
  return lib.find(q).value_or(LoanRule{});
}

// --- Sammanfattningar av egna funktioner -------------------------------------
//
// Härledda ur kroppen. Plats 0 = objektet (*this), plats i+1 = parameter i;
// vägar är relativa platsen. För en pekarlik operand (pekare, iterator, vy,
// lambda) betyder ett inledande `[*]` det operanden pekar på. Absoluta
// platser: målet för en referensmedlem (gemensamt för alla instanser).
using Path = std::vector<const FieldDecl*>;

struct Summary {
  bool known = false;
  std::set<std::pair<unsigned, Path>> writes;
  std::set<Place> absoluteWrites;
  struct Ret {
    int slot;     // -1: `place` är absolut
    Place place;  // slot >= 0: bara vägen används
    bool exclusive;
    auto operator<=>(const Ret&) const = default;
  };
  std::set<Ret> returns;
};

}  // namespace

struct LoanContext::Impl {
  const LoanSummaries& lib;
  std::map<const FunctionDecl*, std::optional<Summary>> summaries;  // nullopt: pågår
  std::map<const FunctionDecl*, Reason> verdicts;
  const Summary* summary(const FunctionDecl& fd);
  Reason run(const FunctionDecl& fd);
};

namespace {

// --- Liveness ----------------------------------------------------------------

// Levande variabler efter varje uttryck (samma som LocalCheck:
// killAtAssign = false, värdet efter satsen).
std::map<const Stmt*, std::set<const VarDecl*>> liveAfter(AnalysisDeclContext& ac) {
  std::map<const Stmt*, std::set<const VarDecl*>> out;
  std::unique_ptr<LiveVariables> lv = LiveVariables::computeLiveness(ac, false);
  if (lv == nullptr) return out;
  struct Collect : LiveVariables::Observer {
    std::map<const Stmt*, std::set<const VarDecl*>>& out;
    explicit Collect(std::map<const Stmt*, std::set<const VarDecl*>>& o) : out(o) {}
    void observeStmt(const Stmt* s, const CFGBlock*, const LiveVariables::LivenessValues& v) override {
      std::set<const VarDecl*>& live = out[s];
      for (const VarDecl* d : v.liveDecls) live.insert(d);
      // STN-84: en levande bindning håller den dolda variabeln och sin
      // hållare levande; det är de som bär lånen (clang räknar bindningen).
      for (const BindingDecl* b : v.liveBindings) {
        if (const auto* dd = dyn_cast_or_null<VarDecl>(b->getDecomposedDecl())) live.insert(dd);
        if (const VarDecl* holding = b->getHoldingVar()) live.insert(holding);
      }
    }
  } collect(out);
  lv->runOnAllBlocks(collect);
  return out;
}

// --- Kontrollen --------------------------------------------------------------

using State = std::map<const VarDecl*, std::set<int>>;

class LoanChecker {
 public:
  LoanChecker(const FunctionDecl& fd, std::map<const Stmt*, std::set<const VarDecl*>> live,
              LoanContext::Impl& ctx, Summary& record)
      : fd_(fd), sm_(fd.getASTContext().getSourceManager()), live_(std::move(live)), ctx_(ctx), record_(record) {}

  Reason run(const CFG& cfg, AnalysisDeclContext& ac) {
    State entry = parameters();
    const auto* order = ac.getAnalysis<PostOrderCFGView>();
    std::map<const CFGBlock*, State> out;
    auto in = [&](const CFGBlock* b) {
      if (b == &cfg.getEntry()) return entry;
      State s;
      for (const CFGBlock::AdjacentBlock& p : b->preds())
        if (const CFGBlock* pb = p.getReachableBlock())
          if (auto it = out.find(pb); it != out.end())
            for (const auto& [v, ids] : it->second) s[v].insert(ids.begin(), ids.end());
      return s;
    };
    for (bool changed = true; changed;) {
      changed = false;
      for (const CFGBlock* b : *order) {
        State s = in(b);
        block(*b, s);
        if (out[b] != s) {
          out[b] = std::move(s);
          changed = true;
        }
      }
    }
    checking_ = true;
    record_.known = true;
    for (const CFGBlock* b : *order) {
      State s = in(b);
      block(*b, s);
      if (error_) break;
    }
    if (error_) record_.known = false;  // efter första felet saknas effekter
    return error_;
  }

 private:
  // --- Tillstånd och lån -----------------------------------------------------

  int issue(const Place& p, bool exclusive) {
    auto key = std::make_pair(p, exclusive);
    if (auto it = loanIds_.find(key); it != loanIds_.end()) return it->second;
    loans_.push_back({p, exclusive});
    return loanIds_[key] = static_cast<int>(loans_.size()) - 1;
  }

  std::set<int> bind(const std::vector<PlaceRef>& refs) {
    std::set<int> ids;
    for (const PlaceRef& r : refs)
      ids.insert(r.loan >= 0 && loans_[r.loan].place == r.place ? r.loan : issue(r.place, r.exclusive));
    return ids;
  }

  State parameters() {
    State s;
    for (const ParmVarDecl* p : fd_.parameters()) {
      QualType t = p->getType();
      if (t->isReferenceType()) {
        s[p] = {issue({rootOf(p, Root::referent), {}}, !t->getPointeeType().isConstQualified())};
      } else if (types_.loanBearing(t)) {
        // Det ett lånebärande värde pekar på finns utanför funktionen.
        bool exclusive = !t->isPointerType() || !t->getPointeeType().isConstQualified();
        s[p] = {issue({rootOf(p, Root::referent), {nullptr}}, exclusive)};
      }
    }
    return s;
  }

  void fail(const Stmt* at, std::string why) {
    if (!checking_ || error_) return;
    unsigned line = at ? sm_.getPresumedLoc(at->getBeginLoc()).getLine() : 0;
    error_ = why + (line ? " (line " + std::to_string(line) + ")" : "");
  }

  void unsupported(const Expr* e, const char* what) {
    fail(e, std::string("cannot follow the loans of ") + what + " (" + e->getStmtClassName() + ")");
  }

  // Den dolda variabeln i `auto [a, b] = e;` heter `[a, b]`.
  static std::string declName(const NamedDecl& d) {
    const auto* dd = dyn_cast<DecompositionDecl>(&d);
    if (dd == nullptr) return d.getNameAsString();
    std::string out = "[";
    for (const BindingDecl* b : dd->bindings()) out += (out.size() > 1 ? ", " : "") + b->getNameAsString();
    return out + "]";
  }

  std::string name(const VarDecl* v) const {
    if (isa<DecompositionDecl>(v)) return "'" + declName(*v) + "'";
    if (v->isImplicit() || v->getName().starts_with("__")) return "the range-for iterator";
    return "'" + v->getNameAsString() + "'";
  }

  std::string text(const Place& p) const {
    std::string s;
    switch (p.root.kind) {
      case Root::object:
      case Root::referent:
      case Root::staticStorage:
        if (p.root == kOutside)
          s = "an object outside the function";
        else if (const auto* nd = dyn_cast_or_null<NamedDecl>(p.root.decl))
          s = declName(*nd);
        else
          s = "a literal";
        break;
      case Root::self: s = "*this"; break;
      case Root::temporary:
      case Root::extended: s = "a temporary"; break;
    }
    for (const FieldDecl* f : p.path) s += f ? "." + f->getNameAsString() : "[*]";
    for (unsigned i = 0; i < p.extra; ++i) s += "[*]";
    return s;
  }

  // Variabler som lever efter `at`, inklusive variabler som lånas av en
  // levande variabel (en referens till en iterator håller iteratorn vid liv).
  std::set<const VarDecl*> liveAt(const Stmt* at, const State& s) const {
    std::set<const VarDecl*> live;
    auto it = at ? live_.find(at) : live_.end();
    if (it == live_.end()) {
      for (const auto& [v, ids] : s) live.insert(v);  // okänt: allt lever
      return live;
    }
    live = it->second;
    for (bool grew = true; grew;) {
      grew = false;
      for (const VarDecl* v : std::set<const VarDecl*>(live))
        if (auto st = s.find(v); st != s.end())
          for (int id : st->second)
            if (const VarDecl* r = rootVar(loans_[id].place)) grew = live.insert(r).second || grew;
    }
    return live;
  }

  // --- Yttre objekt som kan vara samma ----------------------------------------
  //
  // Parametrarnas mål och *this antas vara skilda (anroparen har kontrollerat
  // G3). Målet för en referensmedlem (kontextreferens) har ingen kontrollerat:
  // det kan vara samma som, eller en del av, varje annat yttre objekt av en
  // typ som når det.

  QualType rootType(const Root& r) const {
    if (r.kind == Root::self) {
      if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_)) return md->getThisType()->getPointeeType();
      return {};
    }
    if (r.kind != Root::referent) return {};
    if (const auto* f = dyn_cast_or_null<FieldDecl>(r.decl)) {
      QualType t = f->getType().getNonReferenceType();
      return t->isPointerType() ? t->getPointeeType() : t;  // pekarfält: det det pekar på
    }
    if (const auto* p = dyn_cast_or_null<ParmVarDecl>(r.decl)) {
      QualType t = p->getType();
      if (t->isReferenceType()) return t.getNonReferenceType();
      if (t->isPointerType()) return t->getPointeeType();
    }
    return {};
  }

  static bool external(const Root& r) { return r.kind == Root::referent || r.kind == Root::self; }
  // Målet för en referensmedlem/ett pekarfält, eller något okänt yttre: kan
  // vara samma objekt som en parameter eller *this.
  static bool memberTarget(const Root& r) {
    return r.kind == Root::referent && (r == kOutside || isa_and_nonnull<FieldDecl>(r.decl));
  }

  bool mayAlias(const Root& a, const Root& b) {
    if (a == b) return true;
    if (!external(a) || !external(b) || (!memberTarget(a) && !memberTarget(b))) return false;
    QualType ta = rootType(a), tb = rootType(b);
    if (ta.isNull() || tb.isNull()) return true;
    return types_.reaches(ta, tb) || types_.reaches(tb, ta);
  }

  bool mayOverlap(const Place& a, const Place& b) {
    if (a.root == b.root) return overlap(a, b);
    return mayAlias(a.root, b.root);
  }

  static bool owned(const Place& p) {
    for (const FieldDecl* f : p.path)
      if (f == nullptr) return true;
    return false;
  }

  bool mayInvalidate(const Place& changed, const Place& loan) {
    if (changed.root == loan.root) return invalidates(changed, loan);
    return mayAlias(changed.root, loan.root) && owned(loan);
  }

  // Ett anrop som når ett polymorft objekt (en port, ett gränssnitt): den
  // okända implementationen kan hålla referenser till allt utanför
  // funktionen och ändra det. Lån i ägd lagring i yttre objekt dör.
  void changeOutside(const Stmt* at, const State& s, const std::string& callee) {
    if (checking_) record_.known = false;  // okänt vad porten ändrar
    if (!checking_ || error_) return;
    for (const VarDecl* v : liveAt(at, s)) {
      auto st = s.find(v);
      if (st == s.end()) continue;
      for (int id : st->second)
        if (external(loans_[id].place.root) && owned(loans_[id].place))
          return fail(at, "calls '" + callee + "', which reaches a polymorphic object and may change '" +
                              text(loans_[id].place) + "' while " + name(v) + " borrows it and is used later");
    }
  }

  // --- Sammanfattningen av den här funktionen --------------------------------

  // Platsens plats i sammanfattningen: 0 = *this, i+1 = parameter i, -1 =
  // absolut, nullopt = lokal (ingen effekt utanför), -2 = okänd.
  std::optional<int> slotOf(const Root& r) const {
    switch (r.kind) {
      case Root::self: return 0;
      case Root::referent:
        if (r == kOutside || isa_and_nonnull<FieldDecl>(r.decl)) return -1;
        if (const auto* p = dyn_cast_or_null<ParmVarDecl>(r.decl))
          for (unsigned i = 0; i < fd_.getNumParams(); ++i)
            if (fd_.getParamDecl(i) == p) return static_cast<int>(i) + 1;
        return -2;
      case Root::staticStorage: return -1;
      case Root::temporary:
      case Root::extended: return std::nullopt;
      case Root::object:
        // En egen lokal variabel eller värdeparameter; en fångad variabel
        // från en omgivande funktion (lambdakropp) är okänd.
        if (const auto* vd = dyn_cast_or_null<VarDecl>(r.decl); vd && vd->getParentFunctionOrMethod() == &fd_)
          return std::nullopt;
        return -2;
    }
    return -2;
  }

  // En väg som fortsätter genom ett pekarlikt fält beskriver det fältet
  // pekar på, inte något objektet äger: kan inte uttryckas i sammanfattningen.
  bool throughPointerField(const Path& path) const {
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
      const FieldDecl* f = path[i];
      if (f == nullptr) continue;
      QualType t = f->getType();
      const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
      if (t->isPointerType() || (rd != nullptr && (isGslPointer(rd) || rd->isLambda()))) return true;
    }
    return false;
  }

  void recordWrite(const Place& p) {
    std::optional<int> slot = slotOf(p.root);
    if (!slot) return;
    if (throughPointerField(p.path)) {
      record_.known = false;
      return;
    }
    if (*slot == -2) record_.known = false;
    else if (*slot == -1) record_.absoluteWrites.insert(p);
    else record_.writes.insert({static_cast<unsigned>(*slot), p.path});
  }

  void recordReturns(const std::vector<PlaceRef>& refs) {
    for (const PlaceRef& r : refs) {
      std::optional<int> slot = slotOf(r.place.root);
      if (!slot || *slot == -2 || throughPointerField(r.place.path)) {
        record_.known = false;
        continue;
      }
      // Hopslagna nivåer (extra) sparas som [*]-steg: hos anroparen är det
      // flera nivåer ägd lagring, och behållaren är okänd (STN-77).
      Place rel{{}, r.place.path};
      for (unsigned i = 0; i < r.place.extra; ++i) rel.path.push_back(nullptr);
      record_.returns.insert({*slot, *slot == -1 ? r.place : rel, r.exclusive});
    }
  }

  // G2: platsen ändras.
  void change(const Place& p, const Stmt* at, const State& s) {
    if (checking_) recordWrite(p);
    if (!checking_ || error_) return;
    for (const VarDecl* v : liveAt(at, s)) {
      if (v == consumed_) continue;  // skrivs över av anropets resultat
      auto st = s.find(v);
      if (st == s.end()) continue;
      for (int id : st->second)
        if (mayInvalidate(p, loans_[id].place))
          return fail(at, "changes '" + text(p) + "' while " + name(v) + " borrows '" +
                              text(loans_[id].place) + "' and is used later");
    }
  }

  void change(const std::vector<PlaceRef>& refs, const Stmt* at, const State& s) {
    for (const PlaceRef& r : refs) change(r.place, at, s);
  }

  // --- Uttryck ---------------------------------------------------------------

  static const Expr* strip(const Expr* e) {
    for (;;) {
      e = e->IgnoreParens();
      if (const auto* c = dyn_cast<ImplicitCastExpr>(e)) {
        switch (c->getCastKind()) {
          case CK_NoOp: case CK_DerivedToBase: case CK_UncheckedDerivedToBase:
          case CK_ConstructorConversion: case CK_UserDefinedConversion:
            e = c->getSubExpr();
            continue;
          default:
            return e;
        }
      }
      if (const auto* w = dyn_cast<ExprWithCleanups>(e)) { e = w->getSubExpr(); continue; }
      if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) { e = b->getSubExpr(); continue; }
      if (const auto* d = dyn_cast<CXXDefaultArgExpr>(e)) { e = d->getExpr(); continue; }
      if (const auto* d = dyn_cast<CXXDefaultInitExpr>(e)) { e = d->getExpr(); continue; }
      if (const auto* o = dyn_cast<OpaqueValueExpr>(e); o != nullptr && o->getSourceExpr()) {
        e = o->getSourceExpr();
        continue;
      }
      if (const auto* f = dyn_cast<CXXFunctionalCastExpr>(e); f != nullptr && f->getCastKind() == CK_NoOp) {
        e = f->getSubExpr();
        continue;
      }
      return e;
    }
  }

  // Det ett glvalue-uttryck betecknar.
  std::vector<PlaceRef> places(const Expr* e, const State& s) {
    e = strip(e);
    bool exclusive = !e->getType().isConstQualified();
    if (const auto* dr = dyn_cast<DeclRefExpr>(e)) {
      if (const auto* b = dyn_cast<BindingDecl>(dr->getDecl())) {
        if (const Expr* be = b->getBinding()) return places(be, s);
        unsupported(e, "a structured binding");
        return {};
      }
      const auto* vd = dyn_cast<VarDecl>(dr->getDecl());
      if (vd == nullptr) return {};  // funktion, enumkonstant: inget minne
      if (!vd->hasLocalStorage() && !isa<ParmVarDecl>(vd)) return {{{rootOf(vd, Root::staticStorage), {}}, exclusive}};
      if (vd->getType()->isReferenceType()) {
        if (auto it = s.find(vd); it != s.end()) return held(it->second, exclusive);
        if (const Expr* init = vd->getInit()) return places(init, s);  // t.ex. bindningarnas hållare
        unsupported(e, "a reference that is not bound on this path");
        return {};
      }
      return {{{rootOf(vd, Root::object), {}}, exclusive}};
    }
    if (const auto* m = dyn_cast<MemberExpr>(e)) {
      if (const auto* vd = dyn_cast<VarDecl>(m->getMemberDecl())) return {{{rootOf(vd, Root::staticStorage), {}}, exclusive}};
      const auto* f = dyn_cast<FieldDecl>(m->getMemberDecl());
      if (f == nullptr) return {};
      // En referensmedlem pekar ut ur objektet: dess mål är en egen rot (ett
      // abstrakt objekt per fält, gemensamt för alla instanser: försiktigt).
      if (f->getType()->isReferenceType()) return {{{rootOf(f, Root::referent), {}}, exclusive}};
      std::vector<PlaceRef> base = m->isArrow() ? values(m->getBase(), s) : places(m->getBase(), s);
      const FieldDecl* step = f->getParent()->isUnion() ? nullptr : f;
      for (PlaceRef& r : base) {
        r.place.path.push_back(step);
        r.loan = -1;
        r.exclusive = r.exclusive && exclusive;
      }
      return base;
    }
    if (const auto* u = dyn_cast<UnaryOperator>(e)) {
      if (u->getOpcode() == UO_Deref) return values(u->getSubExpr(), s);
      if (u->getOpcode() == UO_PreInc || u->getOpcode() == UO_PreDec) return places(u->getSubExpr(), s);
    }
    if (const auto* a = dyn_cast<ArraySubscriptExpr>(e)) {
      std::vector<PlaceRef> base = values(a->getBase(), s);
      for (PlaceRef& r : base) r.place = opened(r.place, false), r.loan = -1;
      return base;
    }
    if (const auto* b = dyn_cast<BinaryOperator>(e)) {
      if (b->isAssignmentOp()) return places(b->getLHS(), s);
      if (b->getOpcode() == BO_Comma) return places(b->getRHS(), s);
    }
    if (const auto* c = dyn_cast<AbstractConditionalOperator>(e)) {
      std::vector<PlaceRef> out = places(c->getTrueExpr(), s);
      for (const PlaceRef& r : places(c->getFalseExpr(), s)) out.push_back(r);
      return out;
    }
    if (const auto* t = dyn_cast<MaterializeTemporaryExpr>(e)) {
      Root::Kind k = t->getExtendingDecl() != nullptr ? Root::extended : Root::temporary;
      return {{{rootOf(t, k), {}}, exclusive}};
    }
    if (const auto* c = dyn_cast<CallExpr>(e)) return callResult(c, s);
    if (isa<StringLiteral, PredefinedExpr, CompoundLiteralExpr, CXXTypeidExpr>(e))
      return {{{rootOf(e, Root::staticStorage), {}}, false}};
    unsupported(e, "an lvalue");
    return {};
  }

  std::vector<PlaceRef> held(const std::set<int>& ids, bool exclusive) const {
    std::vector<PlaceRef> out;
    for (int id : ids) out.push_back({loans_[id].place, loans_[id].exclusive && exclusive, id});
    return out;
  }

  // Lånen som objektet i ett glvalue av lånebärande typ håller.
  std::vector<PlaceRef> heldBy(const Expr* e, const State& s) {
    e = strip(e);
    if (!e->isGLValue()) return values(e, s);
    if (const auto* t = dyn_cast<MaterializeTemporaryExpr>(e)) return values(t->getSubExpr(), s);
    return heldIn(places(e, s), e->getType(), e, s);
  }

  // Lånen som objekten på platserna håller (objektens typ: `t`).
  std::vector<PlaceRef> heldIn(const std::vector<PlaceRef>& objects, QualType t, const Expr* e, const State& s) {
    std::vector<PlaceRef> out;
    for (const PlaceRef& p : objects) {
      const VarDecl* vd = p.place.path.empty() ? rootVar(p.place) : nullptr;
      if (vd != nullptr) {
        if (auto it = s.find(vd); it != s.end())
          for (const PlaceRef& r : held(it->second, true)) out.push_back(r);
        continue;
      }
      if ((p.place.root.kind == Root::temporary || p.place.root.kind == Root::extended) && p.place.path.empty()) {
        const auto* t = cast<MaterializeTemporaryExpr>(p.place.root.stmt);  // en temporär: dess värde
        for (const PlaceRef& r : values(t->getSubExpr(), s)) out.push_back(r);
        continue;
      }
      if (p.place.root.kind == Root::referent || p.place.root.kind == Root::self) {
        // Ett objekt utanför funktionen. Dess referensmedlemmar pekar på sina
        // egna rötter; annat lånebärande innehåll (en iterator) pekar på
        // något okänt utanför: ägd lagring under platsen.
        // STN-71: en inre lista i en nästlad initializer_list. Dess array är en
        // temporär hos anroparen som lever och dör med den yttre: ägd lagring
        // under platsen, som elementen i en vector<vector<int>>.
        if (types_.initializerList(t)) {
          out.push_back({opened(p.place), p.exclusive, -1});
          continue;
        }
        bool other = false;
        std::vector<PlaceRef> fields;
        const FieldDecl* last = p.place.path.empty() ? nullptr : p.place.path.back();
        if (t->isPointerType() && last != nullptr) {
          // Ett pekarfält: pekar på sin egen rot (som en referensmedlem).
          fields.push_back({{rootOf(last, Root::referent), {}}, p.exclusive, -1});
        } else {
          fields = referenceMembers(t, other);
        }
        out.insert(out.end(), fields.begin(), fields.end());
        // Annat lånebärande innehåll pekar på något okänt utanför, inte på
        // lagring som objektet äger (h.list kan peka på en parameter).
        if (other) out.push_back({{kOutside, {nullptr}}, p.exclusive, -1});
        else if (fields.empty()) out.push_back({opened(p.place), p.exclusive, -1});
        continue;
      }
      unsupported(e, "a stored loan-bearing value");
    }
    return out;
  }

  // Målen för referensmedlemmarna i en klass (även i baser och värdefält).
  // `other`: klassen bär lån på annat sätt (pekare, iterator).
  std::vector<PlaceRef> referenceMembers(QualType t, bool& other) {
    std::vector<PlaceRef> out;
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition() || isGslPointer(rd) || isGslOwner(rd)) {
      other = other || types_.loanBearing(t);
      return out;
    }
    rd = rd->getDefinition();
    for (const CXXBaseSpecifier& b : rd->bases()) {
      std::vector<PlaceRef> got = referenceMembers(b.getType(), other);
      out.insert(out.end(), got.begin(), got.end());
    }
    for (const FieldDecl* f : rd->fields()) {
      QualType ft = f->getType();
      if (ft->isReferenceType()) {
        out.push_back({{rootOf(f, Root::referent), {}}, !ft->getPointeeType().isConstQualified(), -1});
      } else if (ft->isPointerType()) {
        out.push_back({{rootOf(f, Root::referent), {}}, !ft->getPointeeType().isConstQualified(), -1});
      } else if (types_.loanBearing(ft)) {
        std::vector<PlaceRef> got = referenceMembers(ft, other);
        out.insert(out.end(), got.begin(), got.end());
      }
    }
    return out;
  }

  // Det ett lånebärande värde pekar på (lånen värdet håller).
  std::vector<PlaceRef> values(const Expr* e, const State& s) {
    e = strip(e);
    if (const auto* c = dyn_cast<ImplicitCastExpr>(e)) {
      switch (c->getCastKind()) {
        case CK_LValueToRValue: return heldBy(c->getSubExpr(), s);
        case CK_ArrayToPointerDecay: {
          std::vector<PlaceRef> out = places(c->getSubExpr(), s);
          for (PlaceRef& r : out) r.place = opened(r.place), r.loan = -1;
          return out;
        }
        case CK_NullToPointer: case CK_FunctionToPointerDecay: case CK_IntegralToPointer:
          return {};
        default:
          unsupported(e, "a cast");
          return {};
      }
    }
    if (e->isGLValue() && !e->getType()->isReferenceType() && types_.loanBearing(e->getType()))
      return heldBy(e, s);
    if (const auto* u = dyn_cast<UnaryOperator>(e); u != nullptr && u->getOpcode() == UO_AddrOf)
      return places(u->getSubExpr(), s);
    if (const auto* t = dyn_cast<CXXThisExpr>(e)) {
      bool exclusive = !t->getType()->getPointeeType().isConstQualified();
      return {{{rootOf(&fd_, Root::self), {}}, exclusive}};
    }
    if (const auto* t = dyn_cast<MaterializeTemporaryExpr>(e)) return values(t->getSubExpr(), s);
    if (const auto* il = dyn_cast<CXXStdInitializerListExpr>(e)) {  // pekar på den temporära arrayen
      std::vector<PlaceRef> out = places(il->getSubExpr(), s);
      for (PlaceRef& r : out) r.place = opened(r.place), r.loan = -1;
      return out;
    }
    if (const auto* c = dyn_cast<CallExpr>(e)) return callResult(c, s);
    if (const auto* c = dyn_cast<CXXConstructExpr>(e)) return constructResult(c, s);
    if (const auto* l = dyn_cast<InitListExpr>(e)) {
      std::vector<PlaceRef> out;
      for (const Expr* i : l->inits()) collectInit(i, out, s);
      return out;
    }
    if (const auto* l = dyn_cast<LambdaExpr>(e)) {
      std::vector<PlaceRef> out;
      for (const Expr* i : l->capture_inits())
        if (i != nullptr) collectInit(i, out, s);
      return out;
    }
    if (const auto* c = dyn_cast<AbstractConditionalOperator>(e)) {
      std::vector<PlaceRef> out = values(c->getTrueExpr(), s);
      for (const PlaceRef& r : values(c->getFalseExpr(), s)) out.push_back(r);
      return out;
    }
    // STN-75: range-for över en inbyggd array räknar ut slutet som
    // `__range + N`. Pekaren pekar in i samma lagring som `__range`. Bara för
    // kompilatorns egna loopvariabler, inte för pekararitmetik i koden.
    if (const auto* b = dyn_cast<BinaryOperator>(e);
        b != nullptr && (b->getOpcode() == BO_Add || b->getOpcode() == BO_Sub) && b->getType()->isPointerType()) {
      const Expr* pointer = b->getLHS()->getType()->isPointerType() ? b->getLHS() : b->getRHS();
      const Expr* offset = pointer == b->getLHS() ? b->getRHS() : b->getLHS();
      const auto* dr = dyn_cast<DeclRefExpr>(strip(pointer));
      if (const auto* c = dyn_cast<ImplicitCastExpr>(strip(pointer)); dr == nullptr && c != nullptr)
        dr = dyn_cast<DeclRefExpr>(strip(c->getSubExpr()));
      const auto* vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
      if (vd != nullptr && vd->isImplicit() && offset->getType()->isIntegerType()) return values(pointer, s);
    }
    if (isa<CXXNullPtrLiteralExpr, IntegerLiteral, GNUNullExpr>(e)) return {};
    // T() / {} för en pekare: en nollpekare (`reset(pointer p = pointer())`).
    if (isa<CXXScalarValueInitExpr, ImplicitValueInitExpr>(e) && e->getType()->isPointerType()) return {};
    if (isa<SourceLocExpr, StringLiteral, PredefinedExpr>(e)) return {{{rootOf(e, Root::staticStorage), {}}, false}};
    if (!types_.loanBearing(e->getType())) return {};
    unsupported(e, "a loan-bearing value");
    return {};
  }

  // Initierare av ett fält/en fångst: referens = lån av platsen, lånebärande
  // värde = dess lån, annars inget.
  void collectInit(const Expr* i, std::vector<PlaceRef>& out, const State& s) {
    std::vector<PlaceRef> got;
    if (i->isGLValue() && !types_.loanBearing(i->getType()))
      got = places(i, s);  // referensfångst / referensfält
    else if (types_.loanBearing(i->getType()))
      got = values(i, s);
    out.insert(out.end(), got.begin(), got.end());
  }

  // --- Anrop -----------------------------------------------------------------

  struct Operand {
    const Expr* e;
    QualType param;  // null: objektet, eller variadiskt
    bool object = false;
  };

  // STN-86: två element i samma ägda array med olika konstanta index är skilda
  // objekt (`write_pair(values[0], values[1])`). Platserna har bara [*], så det
  // avgörs på uttrycken: samma form (samma fält, lika många index, samma
  // variabel eller *this) och på någon nivå två heltalskonstanta index som
  // skiljer sig. Ett index utanför arrayen är UB som Silver ska fånga, inte
  // lånemodellen. Allt annat (ett variabelt index, en referens, std::array) är
  // som förut: samma plats.
  bool distinctElements(const Expr* a, const Expr* b) const {
    const ASTContext& ctx = fd_.getASTContext();
    bool differ = false;
    for (;;) {
      a = a->IgnoreParenImpCasts();
      b = b->IgnoreParenImpCasts();
      const auto* sa = dyn_cast<ArraySubscriptExpr>(a);
      const auto* sb = dyn_cast<ArraySubscriptExpr>(b);
      if (sa != nullptr && sb != nullptr) {
        const Expr* ba = sa->getBase()->IgnoreParenImpCasts();
        const Expr* bb = sb->getBase()->IgnoreParenImpCasts();
        if (!ba->getType()->isConstantArrayType() || !bb->getType()->isConstantArrayType()) return false;
        Expr::EvalResult ia, ib;
        if (sa->getIdx()->EvaluateAsInt(ia, ctx) && sb->getIdx()->EvaluateAsInt(ib, ctx) &&
            !llvm::APSInt::isSameValue(ia.Val.getInt(), ib.Val.getInt()))
          differ = true;
        a = ba;
        b = bb;
        continue;
      }
      if (sa != nullptr || sb != nullptr) return false;
      const auto* ma = dyn_cast<MemberExpr>(a);
      const auto* mb = dyn_cast<MemberExpr>(b);
      if (ma != nullptr && mb != nullptr) {
        if (ma->getMemberDecl() != mb->getMemberDecl() || !isa<FieldDecl>(ma->getMemberDecl())) return false;
        a = ma->getBase();
        b = mb->getBase();
        if ((ma->isArrow() || mb->isArrow()) &&
            !(isa<CXXThisExpr>(a->IgnoreParenImpCasts()) && isa<CXXThisExpr>(b->IgnoreParenImpCasts())))
          return false;
        continue;
      }
      if (ma != nullptr || mb != nullptr) return false;
      if (isa<CXXThisExpr>(a) && isa<CXXThisExpr>(b)) return differ;
      const auto* da = dyn_cast<DeclRefExpr>(a);
      const auto* db = dyn_cast<DeclRefExpr>(b);
      return da != nullptr && db != nullptr && da->getDecl() == db->getDecl() && isa<VarDecl>(da->getDecl()) && differ;
    }
  }

  static std::vector<Operand> operands(const CallExpr* c, const FunctionDecl* fd) {
    std::vector<Operand> out;
    unsigned first = 0;
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(c)) {
      out.push_back({mc->getImplicitObjectArgument(), {}, true});
    } else if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(c);
               oc != nullptr && isa_and_nonnull<CXXMethodDecl>(fd) && cast<CXXMethodDecl>(fd)->isInstance()) {
      // En niebloid är en fri funktion: objektet saknar tillstånd.
      if (niebloid(*fd).empty()) out.push_back({oc->getArg(0), {}, true});
      first = 1;
    }
    for (unsigned i = first; i < c->getNumArgs(); ++i) {
      unsigned p = i - first;
      QualType param = fd && p < fd->getNumParams() ? fd->getParamDecl(p)->getType() : QualType();
      out.push_back({c->getArg(i), param, false});
    }
    return out;
  }

  // Platserna en referensoperand betecknar (objektet via pekare: this, p->).
  std::vector<PlaceRef> operandPlaces(const Operand& o, const State& s) {
    if (o.object && o.e->getType()->isPointerType()) return values(o.e, s);
    return places(o.e, s);
  }

  bool byReference(const Operand& o) const {
    return o.object || (!o.param.isNull() && o.param->isReferenceType()) ||
           (o.param.isNull() && o.e->isGLValue());
  }

  bool operandLoanBearing(const Operand& o) {
    QualType t = o.object && o.e->getType()->isPointerType() ? o.e->getType()->getPointeeType() : o.e->getType();
    return types_.loanBearing(t);
  }

  // Sammanfattningen av en egen anropad funktion, eller nullptr (std, virtuell,
  // ingen kropp i TU:n, rekursion, eller modellen avvisar den).
  const Summary* ownSummary(const FunctionDecl* fd) {
    if (fd == nullptr || inStd(*fd)) return nullptr;
    // Bibliotekskod (även __gnu_cxx m.fl.) har kategorisammanfattningar.
    if (fd->getASTContext().getSourceManager().isInSystemHeader(fd->getLocation())) return nullptr;
    if (const auto* md = dyn_cast<CXXMethodDecl>(fd); md != nullptr && md->isVirtual()) return nullptr;
    const FunctionDecl* def = fd->getDefinition();
    if (def == nullptr || !def->hasBody() || def->isTemplated()) return nullptr;
    return ctx_.summary(*def);
  }

  // Operanden för plats `slot` (0 = objektet, i+1 = argument i), eller nullptr.
  static const Operand* operandFor(const std::vector<Operand>& ops, unsigned slot) {
    unsigned arg = 0;
    for (const Operand& o : ops) {
      if (o.object) {
        if (slot == 0) return &o;
        continue;
      }
      if (++arg == slot) return &o;
    }
    return nullptr;
  }

  bool pointerLike(const Operand& o) const {
    if (o.object && o.e->getType()->isPointerType()) return false;  // *this via pekare: objektet
    QualType t = o.e->getType();
    if (t->isPointerType()) return true;
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    return rd != nullptr && (isGslPointer(rd) || rd->isLambda());
  }

  std::vector<PlaceRef> heldByOperand(const Operand& o, const State& s) {
    bool objectPointer = o.object && o.e->getType()->isPointerType();
    if (objectPointer) return heldIn(values(o.e, s), o.e->getType()->getPointeeType(), o.e, s);
    return o.e->isGLValue() ? heldBy(o.e, s) : values(o.e, s);
  }

  // En väg relativt en plats i sammanfattningen, hos anroparen.
  // `viaHeld`: vägen går genom det operanden pekar på, inte operanden själv.
  std::vector<PlaceRef> mapSlot(const std::vector<Operand>& ops, unsigned slot, const Path& path, const State& s,
                                bool* viaHeld = nullptr) {
    const Operand* o = operandFor(ops, slot);
    if (o == nullptr) return {};
    std::vector<PlaceRef> base;
    auto rest = path.begin();
    bool held = !path.empty() && path.front() == nullptr &&
                (pointerLike(*o) || (!byReference(*o) && operandLoanBearing(*o)));
    if (viaHeld != nullptr) *viaHeld = held;
    if (held) {
      base = heldByOperand(*o, s);
      ++rest;
    } else if (byReference(*o)) {
      base = operandPlaces(*o, s);
    }
    for (PlaceRef& b : base) {
      if (rest != path.end()) b.loan = -1;
      b.place.path.insert(b.place.path.end(), rest, path.end());
    }
    return base;
  }

  std::vector<PlaceRef> callResult(const CallExpr* c, const State& s) {
    const FunctionDecl* fd = c->getDirectCallee();
    const LoanRule rule = libraryRuleImpl(ctx_.lib, fd);
    Effect effect = rule.effect;
    std::vector<Operand> ops = operands(c, fd);
    if (const Summary* own = ownSummary(fd)) {
      std::vector<PlaceRef> out;
      for (const Summary::Ret& r : own->returns) {
        if (r.slot < 0) {
          out.push_back({r.place, r.exclusive, -1});
          continue;
        }
        for (PlaceRef& m : mapSlot(ops, static_cast<unsigned>(r.slot), r.place.path, s)) {
          m.exclusive = m.exclusive && r.exclusive;
          out.push_back(m);
        }
      }
      return out;
    }
    if (effect == Effect::identity) return ops.empty() ? std::vector<PlaceRef>{} : places(ops.front().e, s);
    // resultIn(i): en position i det parameter i pekar in i (std::remove ger
    // slutet på det nya intervallet), inget annat.
    if (rule.resultIn && ownSummary(fd) == nullptr) {
      const Operand* o = operandFor(ops, *rule.resultIn + 1);
      if (o != nullptr && operandLoanBearing(*o) && !o->object) return values(o->e, s);
    }
    // Resultatet pekar bara in i objektet (eller arg0): åtkomst, insättning
    // (iteratorn/referensen till det insatta elementet).
    bool onlyFirst = effect == Effect::access || effect == Effect::writeAccess || effect == Effect::insert;
    QualType rt = c->getCallReturnType(fd_.getASTContext());
    bool constResult = rt->isReferenceType() ? rt->getPointeeType().isConstQualified()
                       : rt->isPointerType() ? rt->getPointeeType().isConstQualified()
                                             : false;
    std::vector<PlaceRef> out;
    for (const Operand& o : ops) {
      std::vector<PlaceRef> got;
      if (operandLoanBearing(o)) {
        // En iterator e.d.: resultatet pekar dit operanden pekar.
        got = o.object && !o.e->getType()->isPointerType() ? heldBy(o.e, s) : values(o.e, s);
      } else if (byReference(o)) {
        got = operandPlaces(o, s);
      }
      // Resultatet lånar ägd lagring i det operanden betecknar eller pekar på
      // (get_if(&v) pekar in i varianten, *it in i behållaren).
      bool nested = !operandLoanBearing(o);
      // En skalär har ingen ägd lagring: resultatet kan bara vara den själv
      // (std::max(a, b) ger a eller b), som en referens bunden till a.
      bool scalar = byReference(o) && !operandLoanBearing(o) && o.e->getType()->isScalarType();
      for (PlaceRef& r : got) {
        if (scalar) {
          r.exclusive = r.exclusive && !constResult;
          out.push_back(r);
          continue;
        }
        Place o = opened(r.place, nested);
        if (o != r.place) r.loan = -1;
        r.place = std::move(o);
        r.exclusive = r.exclusive && !constResult;
        out.push_back(r);
      }
      if (onlyFirst) break;
    }
    return out;
  }

  std::vector<PlaceRef> constructResult(const CXXConstructExpr* c, const State& s) {
    const CXXConstructorDecl* ctor = c->getConstructor();
    if ((ctor->isCopyConstructor() || ctor->isMoveConstructor()) && c->getNumArgs() >= 1)
      return heldBy(c->getArg(0), s);  // en kopia håller samma lån
    std::vector<PlaceRef> out;
    for (unsigned i = 0; i < c->getNumArgs(); ++i) {
      const Expr* a = c->getArg(i);
      QualType param = i < ctor->getNumParams() ? ctor->getParamDecl(i)->getType() : QualType();
      std::vector<PlaceRef> got;
      if (types_.loanBearing(a->getType()))
        got = a->isGLValue() ? heldBy(a, s) : values(a, s);
      else if (!param.isNull() && param->isReferenceType())
        for (PlaceRef& r : (got = places(a, s))) r.place = opened(r.place), r.loan = -1;
      out.insert(out.end(), got.begin(), got.end());
    }
    return out;
  }

  // `consumes`: arg0 är en lokal variabel V och anropet tilldelas direkt
  // tillbaka till V (`it = c.erase(it)`). Annars nullptr: då gäller den
  // vanliga kontrollen (V får inte användas efter anropet).
  const VarDecl* consumedVariable(const Expr* call, const std::vector<Operand>& ops) {
    const Operand* arg0 = nullptr;
    for (const Operand& o : ops)
      if (!o.object) {
        arg0 = &o;
        break;
      }
    if (arg0 == nullptr) return nullptr;
    auto local = [&](const Expr* e) -> const VarDecl* {
      for (;;) {
        e = strip(e);
        if (const auto* c = dyn_cast<CXXConstructExpr>(e); c && c->getNumArgs() == 1) {
          e = c->getArg(0);  // iterator -> const_iterator, kopia
          continue;
        }
        if (const auto* t = dyn_cast<MaterializeTemporaryExpr>(e)) {
          e = t->getSubExpr();
          continue;
        }
        if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) {
          e = b->getSubExpr();
          continue;
        }
        break;
      }
      const auto* dr = dyn_cast<DeclRefExpr>(e);
      const auto* vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
      return vd && vd->hasLocalStorage() && !vd->getType()->isReferenceType() ? vd : nullptr;
    };
    const VarDecl* v = local(arg0->e);
    if (v == nullptr) return nullptr;
    // Gå uppåt genom omslag till tilldelningen.
    ASTContext& ctx = fd_.getASTContext();
    const Stmt* cur = call;
    for (;;) {
      auto parents = ctx.getParents(*cur);
      if (parents.size() != 1) return nullptr;
      const Stmt* up = parents[0].get<Stmt>();
      if (up == nullptr) return nullptr;
      if (isa<ImplicitCastExpr, MaterializeTemporaryExpr, CXXBindTemporaryExpr, ExprWithCleanups, ParenExpr>(up) ||
          (isa<CXXConstructExpr>(up) && cast<CXXConstructExpr>(up)->getNumArgs() == 1)) {
        cur = up;
        continue;
      }
      if (const auto* b = dyn_cast<BinaryOperator>(up); b && b->getOpcode() == BO_Assign && b->getRHS() == cur)
        return local(b->getLHS()) == v ? v : nullptr;
      if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(up);
          oc && oc->getOperator() == OO_Equal && oc->getNumArgs() == 2 && oc->getArg(1) == cur)
        return local(oc->getArg(0)) == v ? v : nullptr;
      return nullptr;
    }
  }

  // Den kända vägen till behållaren som en iterator pekar in i: utan den
  // sista nivån ägd lagring (elementen). nullopt om en nivå före den är okänd
  // (ett element i en annan behållare, en union): m[0] och m[1] är båda
  // m[*], och [*] säger inte vilket element. Nått genom en referens (via):
  // vägen fram till bindningen är känd, den gäller ett objekt.
  static std::optional<std::vector<const FieldDecl*>> knownContainer(const Place& p) {
    return known(p, true);
  }
  // Den kända vägen till ett objekt (en behållare som anropet görs på).
  static std::optional<std::vector<const FieldDecl*>> knownObject(const Place& p) { return known(p, false); }

  static std::optional<std::vector<const FieldDecl*>> known(const Place& p, bool elements) {
    if (p.extra >= kMaxExtra) return std::nullopt;
    std::size_t start = 0;
    int extra = p.extra;
    if (p.via != nullptr) {
      if (p.viaExtra > p.extra || p.viaDepth > p.path.size()) return std::nullopt;
      start = p.viaDepth;
      extra -= p.viaExtra;
    }
    std::vector<const FieldDecl*> path = p.path;
    if (elements) {
      if (extra > 0) --extra;
      else if (path.size() > start && path.back() == nullptr) path.pop_back();
    }
    if (extra != 0) return std::nullopt;
    for (std::size_t i = start; i < path.size(); ++i)
      if (path[i] == nullptr) return std::nullopt;
    if (p.via == nullptr)
      for (const FieldDecl* f : path)
        if (f == nullptr) return std::nullopt;
    return path;
  }

  static bool sameBinding(const Place& a, const Place& b) {
    return a.via == b.via && (a.via == nullptr || (a.viaDepth == b.viaDepth && a.viaExtra == b.viaExtra));
  }

  // Måste a och b peka in i samma objekt? (Två iteratorer i ett intervall
  // eller en jämförelse.) Samma rot och samma kända väg. overlap() svarar
  // på "kan"; här krävs "måste". Två rötter som kan alias:a (en
  // kontextreferens och en parameter) kan också vara olika.
  static bool mustBeSameObject(const Place& a, const Place& b) {
    if (a.root != b.root || !sameBinding(a, b)) return false;
    auto x = knownContainer(a), y = knownContainer(b);
    return x && y && *x == *y;
  }

  // Måste lånet `loan` peka in i objektet `object`? (STN-74.)
  static bool mustPointInto(const Place& loan, const Place& object) {
    if (loan.root != object.root || !sameBinding(loan, object)) return false;
    auto x = knownContainer(loan), y = knownObject(object);
    return x && y && *x == *y;
  }

  // nullopt = alla samma; annars ett par som kan vara olika objekt.
  std::optional<std::pair<std::string, std::string>> differentObjects(const std::vector<PlaceRef>& a,
                                                                       const std::vector<PlaceRef>& b) {
    for (const PlaceRef& x : a)
      for (const PlaceRef& y : b)
        if (!mustBeSameObject(x.place, y.place)) return std::pair{text(x.place), text(y.place)};
    return std::nullopt;
  }

  // En fri funktion (eller statisk) i en systemheader, eller en niebloid i
  // std::ranges: std::sort, std::copy, std::ranges::sort.
  bool freeStdFunction(const FunctionDecl* fd) const {
    if (fd == nullptr || !sm_.isInSystemHeader(fd->getLocation()) || !inStd(*fd)) return false;
    if (!niebloid(*fd).empty()) return true;
    const auto* md = dyn_cast<CXXMethodDecl>(fd);
    return (md == nullptr || md->isStatic()) && fd->getOverloadedOperator() == OO_None;
  }

  // Operanderna a och b (argumentnummer + firstArg) är ett intervall enligt
  // sammanfattningen (interval(i,j)).
  static bool inInterval(const LoanRule& rule, std::size_t a, std::size_t b, std::size_t firstArg) {
    if (a < firstArg || b < firstArg) return false;
    std::size_t i = std::min(a, b) - firstArg, j = std::max(a, b) - firstArg;
    for (const auto& [x, y] : rule.intervals)
      if (x == i && y == j) return true;
    return false;
  }

  // En metod i en std-behållare ([[gsl::Owner]]).
  bool isOwnerMethod(const CXXMethodDecl& md) const {
    return sm_.isInSystemHeader(md.getLocation()) && isGslOwner(md.getParent());
  }

  // Behållarens egen iteratortyp (iterator, const_iterator, local_iterator ...).
  static bool ownIterator(const CXXRecordDecl& owner, QualType param) {
    QualType p = param.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    const ASTContext& ast = owner.getASTContext();
    for (const char* n : {"iterator", "const_iterator", "local_iterator", "const_local_iterator",
                          "reverse_iterator", "const_reverse_iterator"}) {
      auto found = owner.lookup(&ast.Idents.get(n));
      for (const NamedDecl* d : found)
        if (const auto* td = dyn_cast<TypedefNameDecl>(d))
          if (ast.hasSameType(td->getUnderlyingType().getCanonicalType().getUnqualifiedType(), p)) return true;
    }
    return false;
  }

  // Effekter av ett anrop: vad skrivs, och överlappar referensoperanderna (G3)?
  void callEffects(const Expr* call, const FunctionDecl* fd, const std::vector<Operand>& ops, State& s) {
    const LoanRule rule = libraryRuleImpl(ctx_.lib, fd);
    const Effect effect = rule.effect;
    if (effect == Effect::identity) return;
    const auto* md = dyn_cast_or_null<CXXMethodDecl>(fd);
    // `it = c.erase(it)`: iteratorn i arg0 dör men skrivs över direkt.
    const VarDecl* consumed = rule.consumes ? consumedVariable(call, ops) : nullptr;
    struct Reset {
      const VarDecl*& v;
      ~Reset() { v = nullptr; }
    } reset{consumed_};
    consumed_ = consumed;
    // Egen funktion med sammanfattning: exakt vad den ändrar, per operand.
    const Summary* own = isa<CallExpr>(call) ? ownSummary(fd) : nullptr;
    std::vector<std::vector<PlaceRef>> ownChanges(ops.size());
    std::vector<char> ownDirect(ops.size(), 0), ownThrough(ops.size(), 0);
    if (own != nullptr) {
      for (const auto& [slot, path] : own->writes)
        if (const Operand* o = operandFor(ops, slot)) {
          bool viaHeld = false;
          std::vector<PlaceRef> m = mapSlot(ops, slot, path, s, &viaHeld);
          std::size_t k = static_cast<std::size_t>(o - ops.data());
          (viaHeld ? ownThrough : ownDirect)[k] = 1;
          ownChanges[k].insert(ownChanges[k].end(), m.begin(), m.end());
        }
    }
    // Operanderna som kan nå samma objekt: referensoperander (det de betecknar)
    // och lånebärande operander (det de pekar på).
    struct Reach {
      std::vector<PlaceRef> places;
      bool written;
      bool loanSet;  // ett lånebärande värde (iterator, span, lambda)
      std::size_t operand;
      bool invalidates = true;  // skrivningen ogiltigförklarar ägd lagring (inte `insert`)
    };
    // Argumentets nummer (utan objektet), för `position`/`range`.
    const std::size_t firstArg = !ops.empty() && ops.front().object ? 1 : 0;
    // copies: en const T&-parameter vars värde läses innan objektet ändras.
    auto copiedIn = [&](std::size_t k) {
      return rule.copies && k >= firstArg && !ops[k].param.isNull() && ops[k].param->isLValueReferenceType() &&
             ops[k].param->getPointeeType().isConstQualified() && !operandLoanBearing(ops[k]);
    };
    auto mayPointIntoObject = [&](std::size_t k) {
      if (k < firstArg) return false;
      if (copiedIn(k)) return true;
      return (rule.position && k == firstArg) || (rule.range && operandLoanBearing(ops[k]));
    };
    std::vector<Reach> refs;
    std::vector<PlaceRef> through;  // skrivs genom lån som operanderna håller
    std::vector<std::vector<PlaceRef>> compared;  // operander till en jämförelse
    bool port = false;
    for (std::size_t k = 0; k < ops.size(); ++k) {
      const Operand& o = ops[k];
      bool loanBearing = operandLoanBearing(o);
      bool written = false;
      std::vector<PlaceRef> held;
      bool objectPointer = o.object && o.e->getType()->isPointerType();
      QualType type = objectPointer ? o.e->getType()->getPointeeType() : o.e->getType();
      if (loanBearing)
        held = objectPointer ? heldIn(values(o.e, s), type, o.e, s)
               : o.e->isGLValue() ? heldBy(o.e, s)
                                  : values(o.e, s);
      // Ett polymorft objekt, eller ett som håller en referens till ett.
      if (const CXXRecordDecl* rd = type->getAsCXXRecordDecl(); rd && rd->hasDefinition() && rd->isPolymorphic())
        port = true;
      for (const PlaceRef& r : held)
        if (const auto* f = dyn_cast_or_null<FieldDecl>(r.place.root.decl); f && r.place.root.kind == Root::referent)
          if (const CXXRecordDecl* rd = f->getType()->getPointeeCXXRecordDecl(); rd && rd->hasDefinition() && rd->isPolymorphic())
            port = true;
      if (own != nullptr) {
        written = ownDirect[k] != 0;
      } else if (o.object) {
        bool nonConst = md == nullptr || !md->isConst();
        written = effect == Effect::self || effect == Effect::writeAccess || effect == Effect::insert ||
                  (effect == Effect::standard && nonConst);
      } else if (effect == Effect::standard || effect == Effect::insert) {
        bool refParam = !o.param.isNull() && o.param->isReferenceType();
        bool constRef = refParam && o.param->getPointeeType().isConstQualified();
        written = refParam && !constRef;
        if (o.param.isNull() && o.e->isGLValue()) written = true;  // variadiskt/okänt: försiktigt
      }
      // En flytt in i anropet förbrukar platsen, vad sammanfattningen än säger
      // om resten (P-d): en icke-const T&&-parameter, eller objektet i en
      // &&-kvalificerad metod, får ta innehållet (`o.value_or(std::move(v))`,
      // `std::move(o).value_or(d)`). Lån in i den ägda lagringen dör.
      if (own == nullptr && !written && !rule.forwards) {
        if (o.object)
          written = md != nullptr && md->getRefQualifier() == RQ_RValue && !md->isConst();
        else
          written = !o.param.isNull() && o.param->isRValueReferenceType() &&
                    !o.param->getPointeeType().isConstQualified();
      }
      if (byReference(o))
        refs.push_back({operandPlaces(o, s), written, false, k, !(o.object && effect == Effect::insert)});
      if (loanBearing) {
        // En lambda (operator() const), proxy eller ett okänt anrop kan skriva
        // genom de exklusiva lånen; sammanfattningar säger när det inte händer.
        bool writesThrough = own != nullptr ? ownThrough[k] != 0
                            : effect == Effect::standard || effect == Effect::insert;
        std::vector<PlaceRef> excl;
        for (const PlaceRef& r : held)
          if (r.exclusive) excl.push_back(r);
        if (writesThrough && own == nullptr) through.insert(through.end(), excl.begin(), excl.end());
        // Iteratorer, vyer och pekare får överlappa varandra (ett intervall);
        // dolda referenser (lambdafångst, referensmedlem) får inte.
        const CXXRecordDecl* rd = type->getAsCXXRecordDecl();
        bool positions = type->isPointerType() || (rd != nullptr && isGslPointer(rd));
        refs.push_back({held, writesThrough && !excl.empty(), positions, k});
        // En jämförelse kräver samma objekt bara för positioner (iteratorer,
        // pekare). En vy (string_view, span) är själv ett intervall, med
        // begin(): `==` jämför innehållet (pork_examples_2/10).
        bool range = rd != nullptr && rd->hasDefinition() &&
                     !rd->getDefinition()->lookup(&rd->getASTContext().Idents.get("begin")).empty();
        compared.push_back(range ? std::vector<PlaceRef>{} : held);
      }
    }
    // STN-74: ett argument av behållarens egen iteratortyp (insert, erase,
    // emplace_hint ...) ska peka in i objektet: allt det lånar ska kunna vara
    // objektet ([sequence.reqmts]: p är en iterator i a). Oberoende av
    // summaries/std.txt: en saknad rad får inte göra modellen osund.
    // splice/splice_after tar avsiktligt iteratorer in i den andra listan:
    // bara positionen (första argumentet) gäller där.
    if (md != nullptr && !ops.empty() && ops.front().object && isOwnerMethod(*md)) {
      std::vector<PlaceRef> object = operandPlaces(ops.front(), s);
      std::string fname = md->getNameAsString();
      bool splice = fname == "splice" || fname == "splice_after";
      for (std::size_t k = firstArg; k < ops.size(); ++k) {
        if (splice && k != firstArg) continue;
        unsigned param = static_cast<unsigned>(k - firstArg);
        if (param >= md->getNumParams() || !ownIterator(*md->getParent(), md->getParamDecl(param)->getType()))
          continue;
        // En mallparameter (insert(p, InputIt i, InputIt j)) som deducerats
        // till behållarens iteratortyp är ett intervall någon annanstans ifrån.
        if (md->getPrimaryTemplate() != nullptr)
          if (const FunctionDecl* pattern = md->getTemplateInstantiationPattern();
              pattern != nullptr && param < pattern->getNumParams() &&
              pattern->getParamDecl(param)->getType().getNonReferenceType()->getAs<TemplateTypeParmType>() != nullptr)
            continue;
        const Operand& o = ops[k];
        std::vector<PlaceRef> held = o.e->isGLValue() ? heldBy(o.e, s) : values(o.e, s);
        std::string name = fd ? fd->getQualifiedNameAsString() : std::string("?");
        if (held.empty() || object.empty())
          return fail(call, "the iterator argument of '" + name + "' must point into its object");
        for (const PlaceRef& h : held) {
          bool into = false;
          for (const PlaceRef& b : object) into = into || mustPointInto(h.place, b.place);
          if (!into)
            return fail(call, "the iterator argument of '" + name + "' must point into '" +
                                  text(object.front().place) + "', not '" + text(h.place) + "'");
        }
      }
    }
    // Jämförelse av två iteratorer: de måste peka in i samma objekt (annars UB).
    if (fd != nullptr && comparison(fd->getOverloadedOperator()) && compared.size() == 2 && ops.size() == 2) {
      // Varje möjligt ursprung ska vara samma objekt (it = c ? v.begin() : w.begin()).
      if (!compared[0].empty() && !compared[1].empty())
        if (auto d = differentObjects(compared[0], compared[1]))
          return fail(call, "compares '" + d->first + "' with '" + d->second + "', which are not known to be the same object");
    }
    // STN-77: argumentrollerna står i summaries/std.txt (interval(i,j)). Utan
    // rad och med två eller fler iteratorer är rollerna okända: avvisas.
    if (freeStdFunction(fd)) {
      std::size_t iterators = 0;
      for (const Reach& r : refs) iterators += r.loanSet ? 1 : 0;
      if (iterators >= 2 && rule.origin.empty())
        return fail(call, "the iterator arguments of '" + fd->getQualifiedNameAsString() +
                              "' have no roles in summaries/std.txt (interval(i,j))");
    }
    // Ett intervall (first, last) pekar in i samma objekt; std::sort(v.begin(),
    // w.end()) är inget intervall.
    if (freeStdFunction(fd))
      for (std::size_t i = 0; i < refs.size(); ++i)
        for (std::size_t j = i + 1; j < refs.size(); ++j) {
          if (!refs[i].loanSet || !refs[j].loanSet || !inInterval(rule, refs[i].operand, refs[j].operand, firstArg))
            continue;
          if (refs[i].places.empty() || refs[j].places.empty()) continue;
          if (auto d = differentObjects(refs[i].places, refs[j].places))
            return fail(call, "the range (first, last) of '" + fd->getQualifiedNameAsString() + "' is '" + d->first +
                                  "' to '" + d->second + "', which are not known to be the same object");
        }
    // G3: två operander som kan nå samma objekt, där en skrivs. Två
    // lånebärande värden får överlappa (ett iteratorpar är ett intervall).
    for (std::size_t i = 0; i < refs.size(); ++i)
      for (std::size_t j = i + 1; j < refs.size(); ++j) {
        if (!refs[i].written && !refs[j].written) continue;
        if (refs[i].operand == refs[j].operand) continue;  // samma operand
        if (refs[i].loanSet && refs[j].loanSet) {
          // STN-77: en std-funktion utan sammanfattning som kan skriva genom
          // iteratorerna: överlappande iteratorer måste vara ett intervall
          // (first, last). std::copy(b, e, d) med d i [b, e) är UB.
          if (own == nullptr && effect == Effect::standard && freeStdFunction(fd) &&
              !inInterval(rule, refs[i].operand, refs[j].operand, firstArg))
            for (const PlaceRef& a : refs[i].places)
              for (const PlaceRef& b : refs[j].places)
                if (mayOverlap(a.place, b.place))
                  return fail(call, "iterator arguments " + std::to_string(refs[i].operand + 1) + " and " +
                                        std::to_string(refs[j].operand + 1) + " of '" + fd->getQualifiedNameAsString() +
                                        "' may point into the same object ('" + text(a.place) +
                                        "') and are not a range (first, last)");
          continue;
        }
        // Sammanfattningen säger att argumentet får peka in i objektet (en position).
        const Operand& oi = ops[refs[i].operand];
        const Operand& oj = ops[refs[j].operand];
        if ((oi.object && mayPointIntoObject(refs[j].operand)) || (oj.object && mayPointIntoObject(refs[i].operand)))
          continue;
        // Positionen och värdet som kopieras in får båda peka in i objektet
        // (v.insert(v.begin(), v[0])).
        if ((copiedIn(refs[i].operand) && mayPointIntoObject(refs[j].operand)) ||
            (copiedIn(refs[j].operand) && mayPointIntoObject(refs[i].operand)))
          continue;
        if (distinctElements(oi.e, oj.e)) continue;  // STN-86: values[0], values[1]
        for (const PlaceRef& a : refs[i].places)
          for (const PlaceRef& b : refs[j].places)
            if (mayOverlap(a.place, b.place))
              return fail(call, "operands '" + text(a.place) + "' and '" + text(b.place) + "' of the call to '" +
                                    (fd ? fd->getQualifiedNameAsString() : std::string("?")) +
                                    "' may be the same object and one is changed");
      }
    if (own != nullptr) {
      for (const auto& c : ownChanges) change(c, call, s);
      for (const Place& p : own->absoluteWrites) change(p, call, s);
    } else {
      for (const Reach& w : refs)
        if (w.written && !w.loanSet && w.invalidates) change(w.places, call, s);
      if (effect == Effect::standard) storedByCall(ops, md, call, s);
    }
    if (port && effect == Effect::standard) changeOutside(call, s, fd ? fd->getQualifiedNameAsString() : "?");
    change(through, call, s);
  }

  // Ett anrop utan sammanfattning kan lagra lån i ett lånebärande objekt som det
  // får ändra (`void point_at(Cursor& out, const std::string& s) { out.text = s; }`):
  // efter anropet lånar objektet också det som de andra operanderna når. En
  // lokal variabel får lånen; en parameter eller *this får inga lån av något som
  // inte överlever funktionen, och sammanfattningen blir okänd (anroparna ser då
  // samma effekt); annat kan modellen inte följa.
  void storedByCall(const std::vector<Operand>& ops, const CXXMethodDecl* md, const Expr* call, State& s) {
    for (std::size_t k = 0; k < ops.size(); ++k) {
      const Operand& o = ops[k];
      QualType target;
      std::vector<PlaceRef> where;
      if (o.object) {
        if (md != nullptr && md->isConst()) continue;
        target = o.e->getType()->isPointerType() ? o.e->getType()->getPointeeType() : o.e->getType();
        where = operandPlaces(o, s);
      } else if (!o.param.isNull() && o.param->isLValueReferenceType()) {
        if (o.param->getPointeeType().isConstQualified()) continue;
        target = o.param->getPointeeType();
        where = places(o.e, s);
      } else if (!o.param.isNull() && o.param->isPointerType()) {
        if (o.param->getPointeeType().isConstQualified()) continue;
        target = o.param->getPointeeType();
        where = values(o.e, s);
      } else if (o.param.isNull() && o.e->isGLValue()) {  // variadiskt/okänt
        target = o.e->getType();
        where = places(o.e, s);
      } else {
        continue;
      }
      if (!types_.loanBearing(target)) continue;
      std::vector<PlaceRef> incoming;
      for (std::size_t j = 0; j < ops.size(); ++j) {
        if (j == k) continue;
        const Operand& other = ops[j];
        if (operandLoanBearing(other)) {
          auto held = other.object && other.e->getType()->isPointerType()
                          ? heldIn(values(other.e, s), other.e->getType()->getPointeeType(), other.e, s)
                      : other.e->isGLValue() ? heldBy(other.e, s)
                                             : values(other.e, s);
          incoming.insert(incoming.end(), held.begin(), held.end());
        }
        if (byReference(other))
          for (PlaceRef r : operandPlaces(other, s)) {
            r.place = opened(r.place);
            r.loan = -1;
            incoming.push_back(r);
          }
      }
      if (incoming.empty()) continue;
      for (const PlaceRef& w : where) {
        const auto* vd = dyn_cast_or_null<VarDecl>(w.place.root.decl);
        if (w.place.root.kind == Root::object && w.place.path.empty() && vd != nullptr && vd->hasLocalStorage() &&
            !vd->getType()->isReferenceType()) {
          std::set<int> more = bind(incoming);
          s[vd].insert(more.begin(), more.end());
        } else if (w.place.root.kind == Root::referent || w.place.root.kind == Root::self) {
          if (checking_) record_.known = false;
          escape(incoming, call, "may store");
        } else {
          return fail(call, "cannot follow the loans the call may store in '" + text(w.place) + "'");
        }
        if (error_) return;
      }
    }
  }

  // --- Satser och element ----------------------------------------------------

  void assignOrigin(const VarDecl* vd, const Expr* init, const Stmt* at, State& s) {
    QualType t = vd->getType();
    std::vector<PlaceRef> refs;
    if (t->isReferenceType()) {
      refs = places(init, s);
      rebinding(vd, s);
      // Ett okänt element (m[*]): bindningen bestämmer ett objekt.
      for (PlaceRef& r : refs) {
        bool unknown = r.place.extra > 0;
        for (const FieldDecl* f : r.place.path) unknown = unknown || f == nullptr;
        if (!unknown || r.place.via != nullptr || r.place.path.size() > 255) continue;
        r.place.via = vd;
        r.place.viaDepth = static_cast<unsigned char>(r.place.path.size());
        r.place.viaExtra = r.place.extra;
        r.loan = -1;
      }
    } else if (types_.loanBearing(t)) {
      refs = init->isGLValue() ? heldBy(init, s) : values(init, s);
    } else {
      return;
    }
    s[vd] = bind(refs);
    if (!checking_ || error_) return;
    auto live = liveAt(at, s);
    if (!live.contains(vd)) return;
    for (int id : s[vd])
      if (loans_[id].place.root.kind == Root::temporary)
        return fail(at, name(vd) + " borrows a temporary that is destroyed at the end of the full-expression");
  }

  // Referensen `vd` binds på nytt (nästa varv i en loop): lån som togs
  // genom den förra bindningen gäller inte längre samma objekt.
  void rebinding(const VarDecl* vd, State& s) {
    for (auto& [v, ids] : s) {
      std::set<int> next;
      for (int id : ids) {
        if (loans_[id].place.via != vd) {
          next.insert(id);
          continue;
        }
        Place p = loans_[id].place;
        p.via = nullptr;
        p.viaDepth = p.viaExtra = 0;
        next.insert(issue(p, loans_[id].exclusive));
      }
      ids = std::move(next);
    }
  }

  // Ett lån av något som inte överlever funktionen: lokal variabel,
  // värdeparameter eller temporär.
  void escape(const std::vector<PlaceRef>& refs, const Stmt* at, const char* how) {
    for (const PlaceRef& r : refs) {
      Root::Kind k = r.place.root.kind;
      if (k == Root::temporary || k == Root::extended || k == Root::object)
        return fail(at, std::string(how) + " a loan of '" + text(r.place) + "', which does not outlive the function");
    }
  }

  void stmt(const Stmt* st, State& s) {
    if (const auto* ds = dyn_cast<DeclStmt>(st)) {
      for (const Decl* d : ds->decls()) {
        const auto* vd = dyn_cast<VarDecl>(d);
        const Expr* init = vd ? vd->getInit() : nullptr;
        if (init == nullptr) continue;
        if (vd->hasLocalStorage()) {
          assignOrigin(vd, init, st, s);
        } else if (vd->getType()->isReferenceType() || types_.loanBearing(vd->getType())) {
          // En statisk lokal variabel överlever funktionen: som en lagring.
          escape(vd->getType()->isReferenceType() ? places(init, s) : values(init, s), st, "stores");
        }
      }
      return;
    }
    if (const auto* b = dyn_cast<BinaryOperator>(st); b != nullptr && b->isAssignmentOp()) {
      std::vector<PlaceRef> target = places(b->getLHS(), s);
      change(target, st, s);
      const Expr* lhs = strip(b->getLHS());
      const auto* dr = dyn_cast<DeclRefExpr>(lhs);
      const auto* vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
      bool origin = vd != nullptr && vd->hasLocalStorage() && !vd->getType()->isReferenceType();
      if (origin && b->getOpcode() == BO_Assign && types_.loanBearing(vd->getType())) {
        s[vd] = bind(values(b->getRHS(), s));
      } else if (b->getOpcode() == BO_Assign && types_.loanBearing(b->getLHS()->getType())) {
        escape(values(b->getRHS(), s), st, "stores");
        if (!origin) storeOutside(target, values(b->getRHS(), s), st);
      }
      return;
    }
    if (const auto* u = dyn_cast<UnaryOperator>(st); u != nullptr && u->isIncrementDecrementOp()) {
      change(places(u->getSubExpr(), s), st, s);
      return;
    }
    if (const auto* c = dyn_cast<CallExpr>(st)) {
      const FunctionDecl* fd = c->getDirectCallee();
      std::vector<Operand> ops = operands(c, fd);
      callEffects(c, fd, ops, s);
      // Tilldelning till en lånebärande lokal variabel: it = m.find(k). Till
      // något annat (ett fält, `out.text = t`): som en inbyggd tilldelning, en
      // lagring som modellen kontrollerar men inte följer (storeOutside).
      if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(c);
          oc != nullptr && oc->getOperator() == OO_Equal && oc->getNumArgs() == 2 &&
          types_.loanBearing(oc->getArg(0)->getType())) {
        const auto* dr = dyn_cast<DeclRefExpr>(strip(oc->getArg(0)));
        const auto* vd = dr != nullptr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
        std::vector<PlaceRef> rhs = oc->getArg(1)->isGLValue() ? heldBy(oc->getArg(1), s) : values(oc->getArg(1), s);
        if (vd != nullptr && vd->hasLocalStorage() && !vd->getType()->isReferenceType()) {
          s[vd] = bind(rhs);
        } else {
          escape(rhs, st, "stores");
          if (!error_) storeOutside(places(oc->getArg(0), s), rhs, st);
        }
      }
      return;
    }
    if (const auto* c = dyn_cast<CXXConstructExpr>(st)) {
      std::vector<Operand> ops;
      const CXXConstructorDecl* ctor = c->getConstructor();
      for (unsigned i = 0; i < c->getNumArgs(); ++i)
        ops.push_back({c->getArg(i), i < ctor->getNumParams() ? ctor->getParamDecl(i)->getType() : QualType(), false});
      callEffects(c, ctor, ops, s);
      return;
    }
    if (const auto* r = dyn_cast<ReturnStmt>(st)) {
      const Expr* v = r->getRetValue();
      if (v == nullptr) return;
      QualType rt = fd_.getReturnType();
      std::vector<PlaceRef> refs;
      if (rt->isReferenceType())
        refs = places(v, s);
      else if (types_.loanBearing(rt))
        refs = values(v, s);
      escape(refs, st, "returns");
      if (checking_) recordReturns(refs);
      return;
    }
    // STN-83: ett kast avslutar funktionen (ingen användning efteråt). Det
    // kastade objektet får inte bära lån ut ur funktionen.
    if (const auto* te = dyn_cast<CXXThrowExpr>(st)) {
      const Expr* op = te->getSubExpr();
      if (op == nullptr) {
        fail(st, "rethrow");
        return;
      }
      for (;;) {  // T("...") är en konstruktion: följ den
        op = op->IgnoreParens();
        if (const auto* x = dyn_cast<CXXFunctionalCastExpr>(op)) op = x->getSubExpr();
        else if (const auto* x = dyn_cast<CXXBindTemporaryExpr>(op)) op = x->getSubExpr();
        else if (const auto* x = dyn_cast<MaterializeTemporaryExpr>(op)) op = x->getSubExpr();
        else break;
      }
      if (types_.loanBearing(op->getType()))
        for (const PlaceRef& r : values(op, s))
          if (r.place.root.kind != Root::staticStorage || r.place.root.decl != nullptr) {
            fail(st, "throws an object that borrows '" + text(r.place) + "'");
            return;
          }
      return;
    }
    // Tillåtelselista (designbeslut): ett element som ingen gren
    // ovan hanterar måste vara känt för att sakna låneeffekt, annars nej.
    if (!withoutLoanEffect(st))
      fail(st, std::string("cannot follow the loans of ") + st->getStmtClassName());
  }

  // Noder som bara läser eller räknar: de skapar, flyttar eller ändrar inga
  // lån. Det de betecknar följs när en hanterad nod använder dem.
  static bool withoutLoanEffect(const Stmt* st) {
    if (const auto* u = dyn_cast<UnaryOperator>(st)) {
      switch (u->getOpcode()) {
        case UO_Deref: case UO_AddrOf: case UO_Plus: case UO_Minus: case UO_Not: case UO_LNot:
        case UO_Real: case UO_Imag: case UO_Extension:
          return true;
        default:
          return false;  // ++/-- hanteras ovan, co_await m.fl. är okända
      }
    }
    if (const auto* c = dyn_cast<ExplicitCastExpr>(st))
      return isa<CStyleCastExpr, CXXStaticCastExpr, CXXFunctionalCastExpr>(c) &&
             c->getCastKind() != CK_BitCast && c->getCastKind() != CK_IntegralToPointer &&
             c->getCastKind() != CK_LValueBitCast;
    return isa<IntegerLiteral, FloatingLiteral, CharacterLiteral, StringLiteral, CXXBoolLiteralExpr,
               CXXNullPtrLiteralExpr, GNUNullExpr, SourceLocExpr, PredefinedExpr, DeclRefExpr, MemberExpr,
               CXXThisExpr, ParenExpr, ImplicitCastExpr, BinaryOperator, AbstractConditionalOperator,
               OpaqueValueExpr, ArraySubscriptExpr, MaterializeTemporaryExpr, CXXBindTemporaryExpr,
               ExprWithCleanups, CXXDefaultArgExpr, CXXDefaultInitExpr, InitListExpr, ImplicitValueInitExpr,
               CXXScalarValueInitExpr, LambdaExpr, CXXStdInitializerListExpr, UnaryExprOrTypeTraitExpr,
               CXXNoexceptExpr, TypeTraitExpr, ConstantExpr, SubstNonTypeTemplateParmExpr, ArrayInitLoopExpr,
               ArrayInitIndexExpr, CXXRewrittenBinaryOperator, CXXParenListInitExpr, NullStmt>(st) ||
           (isa<AttributedStmt>(st) && isHintOnly(*llvm::cast<AttributedStmt>(st)));
  }

  // Ett lån lagras i något annat än en lokal variabel (ett fält, ett element):
  // modellen kan inte följa det. Lån av parametrar och *this är tillåtna
  // (de lever längre än funktionen), lokala inte.
  void storeOutside(const std::vector<PlaceRef>& target, const std::vector<PlaceRef>& refs, const Stmt* at) {
    // Ett sparat lån är en effekt som sammanfattningen inte uttrycker.
    if (checking_ && !refs.empty()) record_.known = false;
    for (const PlaceRef& r : refs)
      if (r.place.root.kind != Root::referent && r.place.root.kind != Root::self &&
          r.place.root.kind != Root::staticStorage)
        return fail(at, "stores a loan of '" + text(r.place) + "' in '" +
                            (target.empty() ? std::string("?") : text(target.front().place)) + "'");
  }

  void initializer(const CXXCtorInitializer& init, State& s) {
    const FieldDecl* f = init.getMember();
    if (f == nullptr || init.getInit() == nullptr) return;
    std::vector<PlaceRef> refs;
    if (f->getType()->isReferenceType())
      refs = places(init.getInit(), s);
    else if (types_.loanBearing(f->getType()))
      refs = values(init.getInit(), s);
    escape(refs, init.getInit(), "stores");
  }

  void block(const CFGBlock& b, State& s) {
    const Stmt* last = nullptr;
    for (const CFGElement& e : b) {
      if (auto cs = e.getAs<CFGStmt>()) {
        stmt(cs->getStmt(), s);
        last = cs->getStmt();
      } else if (auto ci = e.getAs<CFGInitializer>()) {
        initializer(*ci->getInitializer(), s);
      } else if (auto le = e.getAs<CFGLifetimeEnds>()) {
        const VarDecl* vd = le->getVarDecl();
        if (vd == nullptr) continue;
        Place dead{rootOf(vd, Root::object), {}};
        if (checking_ && !error_) {
          std::set<const VarDecl*> live = last ? liveAt(last, s) : std::set<const VarDecl*>{};
          if (last == nullptr)
            for (const auto& [v, ids] : s) live.insert(v);
          for (const VarDecl* v : live) {
            if (v == vd) continue;
            if (auto st = s.find(v); st != s.end())
              for (int id : st->second)
                if (ends(dead, loans_[id].place))
                  fail(le->getTriggerStmt(), name(v) + " borrows '" + text(loans_[id].place) +
                                                 "', which goes out of scope while it is used later");
          }
        }
        s.erase(vd);
      }
    }
  }

  const FunctionDecl& fd_;
  const SourceManager& sm_;
  std::map<const Stmt*, std::set<const VarDecl*>> live_;
  LoanContext::Impl& ctx_;
  Summary& record_;
  const VarDecl* consumed_ = nullptr;  // under ett `consumes`-anrop
  Types types_;
  std::vector<Loan> loans_;
  std::map<std::pair<Place, bool>, int> loanIds_;
  bool checking_ = false;
  Reason error_;
};

}  // namespace

LoanRule libraryRule(const LoanSummaries& lib, const FunctionDecl* fd) { return libraryRuleImpl(lib, fd); }

bool isNiebloid(const FunctionDecl& fd) { return !niebloid(fd).empty(); }

Reason LoanContext::Impl::run(const FunctionDecl& fd) {
  Summary summary;
  Reason verdict;
  if (fd.isTemplated() || !fd.hasBody()) {
    verdict = std::nullopt;
  } else {
    AnalysisDeclContextManager mgr(fd.getASTContext());
    CFG::BuildOptions& opts = mgr.getCFGBuildOptions();
    opts.setAllAlwaysAdd();
    opts.AddLifetime = true;
    opts.AddInitializers = true;
    AnalysisDeclContext* ac = mgr.getContext(&fd);
    CFG* cfg = ac != nullptr ? ac->getCFG() : nullptr;
    if (cfg == nullptr) {
      verdict = std::string("cannot build the control-flow graph");
    } else {
      restoreCoveredSwitchDefaults(*cfg);
      LoanChecker checker(fd, liveAfter(*ac), *this, summary);
      verdict = checker.run(*cfg, *ac);
    }
  }
  summaries[&fd] = summary;
  verdicts[&fd] = verdict;
  return verdict;
}

const Summary* LoanContext::Impl::summary(const FunctionDecl& fd) {
  if (auto it = summaries.find(&fd); it != summaries.end())
    return it->second && it->second->known ? &*it->second : nullptr;
  summaries[&fd] = std::nullopt;  // pågår: rekursion får standarden
  run(fd);
  const std::optional<Summary>& s = summaries.at(&fd);
  return s && s->known ? &*s : nullptr;
}

LoanContext::LoanContext(const LoanSummaries& lib) : impl_(std::make_unique<Impl>(Impl{lib, {}, {}})) {}
LoanContext::~LoanContext() = default;

Reason LoanContext::check(const FunctionDecl& fd) {
  if (auto it = impl_->verdicts.find(&fd); it != impl_->verdicts.end()) return it->second;
  return impl_->run(fd);
}

Reason checkLoans(const FunctionDecl& fd) {
  static const LoanSummaries lib = [] {
    auto loaded = loadLoanSummaries();
    if (!loaded) llvm::report_fatal_error(llvm::StringRef("pork: " + loaded.error()));
    return std::move(*loaded);
  }();
  LoanContext ctx(lib);
  return ctx.check(fd);
}

}  // namespace pork
