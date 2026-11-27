#include "FlowBuild.hpp"

#include "Dispatch.hpp"
#include "EnumSwitch.hpp"
#include "LocalCheck.hpp"

#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMap.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Analysis/AnalysisDeclContext.h>
#include <clang/Analysis/CFG.h>

#include <map>
#include <set>
#include <optional>

using namespace clang;

namespace pork {

namespace {

// Ett värde som kan skriva någon annanstans: en pekare, en iterator, en vy
// med skrivbara element, en klass med referens- eller pekarfält. En skrivning
// genom det syns inte som en plats, så flödet räknas inte ut.
bool refLike(QualType t) {
  t = t.getNonReferenceType().getCanonicalType();
  if (t->isPointerType()) return !t->getPointeeType().isConstQualified();
  const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
  if (rd == nullptr || !rd->hasDefinition()) return false;
  rd = rd->getDefinition();
  std::string name = rd->getQualifiedNameAsString();
  if (name == "std::span" || name == "std::reference_wrapper" || name == "std::basic_string_view") {
    const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(rd);
    return spec == nullptr || spec->getTemplateArgs().size() == 0 ||
           spec->getTemplateArgs()[0].getKind() != TemplateArgument::Type ||
           !spec->getTemplateArgs()[0].getAsType().isConstQualified();
  }
  // Iteratorer: const_iterator pekar på const (operator* ger const T&).
  if (!rd->lookup(&rd->getASTContext().Idents.get("iterator_category")).empty()) {
    for (const NamedDecl* d : rd->lookup(rd->getASTContext().DeclarationNames.getCXXOperatorName(OO_Star)))
      if (const auto* m = dyn_cast<CXXMethodDecl>(d->getUnderlyingDecl()))
        return !m->getReturnType().getNonReferenceType().isConstQualified();
    return true;
  }
  for (const FieldDecl* f : rd->fields())
    if (f->getType()->isReferenceType() || f->getType()->isPointerType()) return true;
  return false;
}

class Builder {
 public:
  explicit Builder(const FunctionDecl& fd) : fd_(fd) {}

  FlowGraph run() {
    if (!fd_.hasBody()) return fail("no body");
    if (isa<CXXDestructorDecl>(fd_)) return fail("destructor");
    params();
    requirements();
    if (!g_.unsupported.empty()) return g_;
    AnalysisDeclContextManager mgr(fd_.getASTContext());
    CFG::BuildOptions& opts = mgr.getCFGBuildOptions();
    opts.setAllAlwaysAdd();
    opts.AddInitializers = true;
    opts.AddImplicitDtors = true;
    opts.AddTemporaryDtors = true;
    AnalysisDeclContext* ac = mgr.getContext(&fd_);
    CFG* cfg = ac != nullptr ? ac->getCFG() : nullptr;
    if (cfg == nullptr) return fail("cannot build the control-flow graph");
    restoreCoveredSwitchDefaults(*cfg);
    pm_ = std::make_unique<ParentMap>(fd_.getBody());
    if (const auto* ctor = dyn_cast<CXXConstructorDecl>(&fd_))
      for (const CXXCtorInitializer* init : ctor->inits())
        if (init->getInit() != nullptr) pm_->addStmt(init->getInit());

    std::map<unsigned, unsigned> index;  // CFG-blockets id -> index i grafen
    for (const CFGBlock* b : *cfg) index.emplace(b->getBlockID(), static_cast<unsigned>(index.size()));
    g_.blocks.resize(index.size());
    g_.entry = index.at(cfg->getEntry().getBlockID());
    g_.exit = index.at(cfg->getExit().getBlockID());
    // Blocken i ordning efter id, baklänges (clang numrerar från utgången):
    // en lokal referens binds före användningen i källordning.
    std::vector<const CFGBlock*> order(cfg->begin(), cfg->end());
    std::ranges::sort(order, [](const CFGBlock* a, const CFGBlock* b) { return a->getBlockID() > b->getBlockID(); });
    for (const CFGBlock* b : order) {
      block_ = &g_.blocks[index.at(b->getBlockID())];
      throws_ = false;
      const Stmt* cond = b->getTerminatorCondition();
      if (const auto* ce = dyn_cast_or_null<Expr>(cond)) cond = ce->IgnoreParens();
      for (const CFGElement& e : *b) {
        element(e, cond);
        if (!g_.unsupported.empty()) return g_;
      }
      if (const auto* c = dyn_cast_or_null<Expr>(cond)) {
        block_->cond = eval(c);
        // Bara satsernas villkor: i `a && b` och `c ? x : y` är grenen en del av
        // ett värde, och det värdet har eller saknar verkan som helhet.
        if (isa_and_nonnull<IfStmt, WhileStmt, ForStmt, DoStmt, SwitchStmt, CXXForRangeStmt>(b->getTerminatorStmt()))
          block_->condNote = "the condition at " + lineOf(c);
      }
      // Ett kast (STN-83) är ingen normal retur: blocket leder ingenstans.
      if (!throws_)
        for (const CFGBlock::AdjacentBlock& s : b->succs())
          if (const CFGBlock* sb = s.getReachableBlock()) block_->succs.push_back(index.at(sb->getBlockID()));
      if (!g_.unsupported.empty()) return g_;
    }
    return g_;
  }

 private:
  // BRZ-1: PORK_DEPENDS bland de inledande satserna (LocalCheck avvisar dem
  // på andra ställen). Platserna i flödets namn (place()).
  void requirements() {
    const auto* body = dyn_cast_or_null<CompoundStmt>(fd_.getBody());
    if (body == nullptr) return;
    auto shown = [](std::string p) {
      std::string out;
      bool skip = false;
      for (char c : p) {
        if (c == '@') skip = true;
        else if (c == '.' || c == '[') skip = false;
        if (!skip) out += c;
      }
      return out;
    };
    for (const CallExpr* c : leadingRequirements(fd_)) {
      bool result = false;
      flowRequirement(c, &result);
      FlowRequirement r;
      unsigned first = 0;
      if (result) {
        r.output = "return";
        if (fd_.getReturnType()->isVoidType()) r.error = "PORK_DEPENDS_RESULT: the function returns nothing";
      } else {
        first = 1;
        bool weak = false;
        auto p = c->getNumArgs() > 0 ? place(c->getArg(0), weak) : std::nullopt;
        if (!p) {
          r.error = "PORK_DEPENDS: argument 1 is not a parameter, a field path or a field of *this";
        } else {
          r.output = *p;
          std::string root = p->substr(0, std::min(p->find('.'), p->find('[')));
          auto it = std::ranges::find(g_.params, root);
          if (it == g_.params.end() || !g_.outputs[static_cast<std::size_t>(it - g_.params.begin())])
            r.error = "PORK_DEPENDS: '" + shown(*p) + "' is not an output";
        }
      }
      for (unsigned i = first; i < c->getNumArgs() && r.error.empty(); ++i) {
        bool weak = false;
        auto p = place(c->getArg(i), weak);
        std::string root = p ? p->substr(0, std::min(p->find('.'), p->find('['))) : "";
        if (!p || std::ranges::find(g_.params, root) == g_.params.end())
          r.error = "PORK_DEPENDS: argument " + std::to_string(i + 1) +
                    " is not a parameter, a field path or a field of *this";
        else
          r.allowed.push_back(*p);
      }
      g_.requirements.push_back(std::move(r));
    }
  }

  FlowGraph fail(std::string why) {
    if (g_.unsupported.empty()) g_.unsupported = std::move(why);
    return g_;
  }

  void params() {
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd_); md != nullptr && md->isInstance()) {
      g_.params.push_back("this");
      g_.outputs.push_back(isa<CXXConstructorDecl>(md) || !md->isConst());
    }
    for (unsigned i = 0; i < fd_.getNumParams(); ++i) {
      const ParmVarDecl* p = fd_.getParamDecl(i);
      std::string name = p->getName().empty() ? "#param" + std::to_string(i) : p->getNameAsString();
      QualType t = p->getType();
      if (refLike(t)) fail("parameter '" + name + "' can write elsewhere (pointer, iterator or view)");
      g_.params.push_back(name);
      g_.outputs.push_back(t->isReferenceType() && !t->getPointeeType().isConstQualified());
    }
    g_.returnsValue = !fd_.getReturnType()->isVoidType() && !isa<CXXConstructorDecl>(fd_);
  }

  // Lokaler får unika namn: två lokaler som heter x i olika block är olika platser.
  std::string localName(const VarDecl* vd) {
    if (const auto* p = dyn_cast<ParmVarDecl>(vd)) {
      for (unsigned i = 0; i < fd_.getNumParams(); ++i)
        if (fd_.getParamDecl(i) == p) return g_.params[i + (g_.params.size() - fd_.getNumParams())];
    }
    auto [it, fresh] = locals_.try_emplace(vd, "");
    if (fresh) it->second = vd->getNameAsString() + "~" + std::to_string(locals_.size());
    return it->second;
  }

  // Platsen ett uttryck betecknar. weak: ett element (v[i]), och skrivningen
  // lämnar de andra elementen.
  // STN-84: en structured binding betecknar clangs bindningsuttryck: ett fält
  // i den dolda variabeln (`[a, b].x`) eller hållarreferensen (std::get).
  static const Expr* bindingExpr(const DeclRefExpr* d) {
    const auto* b = dyn_cast<BindingDecl>(d->getDecl());
    return b != nullptr ? b->getBinding() : nullptr;
  }

  // STN-84: hållaren för en tuple-lik bindning (`auto [a, b] = pair;`) är en
  // referens initierad med std::get<I>(dold). Den pekar på ett element av den
  // dolda variabeln; vilket säger flödet inte (svagt, dold[*]), som Stone.
  std::optional<std::string> holdingElement(const VarDecl* vd) {
    if (!vd->isImplicit() || vd->getInit() == nullptr) return std::nullopt;
    const DeclRefExpr* d = getOnDecomposition(vd->getInit());
    if (d == nullptr) return std::nullopt;
    if (auto p = place(d)) return *p + "[*]";
    return std::nullopt;
  }

  // `std::get<I>(dold)`: bara kompilatorn skriver det (användaren kan inte
  // namnge den dolda variabeln). Ger dold.
  static const DeclRefExpr* getOnDecomposition(const Expr* e) {
    const auto* call = dyn_cast<CallExpr>(e->IgnoreImplicit());
    const FunctionDecl* callee = call != nullptr ? call->getDirectCallee() : nullptr;
    if (callee == nullptr || !callee->isInStdNamespace() || callee->getIdentifier() == nullptr ||
        callee->getName() != "get" || call->getNumArgs() != 1)
      return nullptr;
    const auto* d = dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
    return d != nullptr && isa<DecompositionDecl>(d->getDecl()) ? d : nullptr;
  }

  std::optional<std::string> place(const Expr* e, bool& weak) {
    e = e->IgnoreParenImpCasts();
    if (const auto* d = dyn_cast<DeclRefExpr>(e); d != nullptr && bindingExpr(d) != nullptr)
      return place(bindingExpr(d), weak);
    if (const auto* d = dyn_cast<DeclRefExpr>(e)) {
      const auto* vd = dyn_cast<VarDecl>(d->getDecl());
      if (vd == nullptr || !vd->hasLocalStorage()) return std::nullopt;
      // Range-for över en C-array: kompilatorns __begin/__end är råa pekare.
      // `++__begin` skriver bara variabeln själv; elementen nås genom
      // loopvariabeln (rangeElement, c[*]), och användaren kan inte namnge
      // dem. (Iteratorer över behållare: jämförelsen __begin != __end är ett
      // anrop med iteratorargument, som flödet inte godtar än.)
      if (rangeIterator(vd)) return localName(vd);
      if (refLike(vd->getType())) return std::nullopt;  // pekar någon annanstans: inget mål
      if (vd->getType()->isReferenceType() && !isa<ParmVarDecl>(vd)) {
        if (auto a = aliases_.find(vd); a != aliases_.end()) return a->second;
        return std::nullopt;
      }
      return localName(vd);
    }
    if (isa<CXXThisExpr>(e)) return std::nullopt;  // pekaren själv
    if (const auto* u = dyn_cast<UnaryOperator>(e); u != nullptr && u->getOpcode() == UO_Deref &&
                                                     isa<CXXThisExpr>(u->getSubExpr()->IgnoreParenImpCasts()))
      return std::string("this");
    if (const auto* m = dyn_cast<MemberExpr>(e)) {
      const auto* field = dyn_cast<FieldDecl>(m->getMemberDecl());
      if (field == nullptr) return std::nullopt;
      std::optional<std::string> base;
      if (m->isArrow()) {
        if (isa<CXXThisExpr>(m->getBase()->IgnoreParenImpCasts())) base = "this";
      } else {
        base = place(m->getBase(), weak);
      }
      if (!base) return std::nullopt;
      return *base + "." + fieldId(*field);
    }
    if (const auto* a = dyn_cast<ArraySubscriptExpr>(e)) {
      auto base = place(a->getBase(), weak);
      if (!base) return std::nullopt;
      weak = true;
      return *base + "[*]";
    }
    if (const auto* op = dyn_cast<CXXOperatorCallExpr>(e);
        op != nullptr && op->getOperator() == OO_Subscript && op->getNumArgs() == 2) {
      auto base = place(op->getArg(0), weak);
      if (!base) return std::nullopt;
      weak = true;
      return *base + "[*]";
    }
    // std::get<I>(t) som lvalue (STN-37): fältet first/second i ett std::pair,
    // annars ett element av t (svagt, t[*]).
    if (const auto* call = dyn_cast<CallExpr>(e); call != nullptr && call->getNumArgs() == 1 &&
                                                   !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(call)) {
      const FunctionDecl* callee = call->getDirectCallee();
      if (callee == nullptr || !callee->isInStdNamespace() || callee->getIdentifier() == nullptr ||
          callee->getName() != "get" || !callee->getReturnType()->isLValueReferenceType())
        return std::nullopt;
      auto base = place(call->getArg(0), weak);
      if (!base) return std::nullopt;
      if (const FieldDecl* f = pairField(*call)) return *base + "." + fieldId(*f);
      weak = true;
      return *base + "[*]";
    }
    return std::nullopt;
  }

  // std::get<0>/<1> på ett std::pair: fältet first eller second.
  static const FieldDecl* pairField(const CallExpr& call) {
    const FunctionDecl* callee = call.getDirectCallee();
    const TemplateArgumentList* args = callee->getTemplateSpecializationArgs();
    const auto* rd = call.getArg(0)->getType().getNonReferenceType()->getAsCXXRecordDecl();
    if (args == nullptr || args->size() == 0 || args->get(0).getKind() != TemplateArgument::Integral || rd == nullptr ||
        rd->getName() != "pair" || !rd->isInStdNamespace())
      return nullptr;
    llvm::APSInt index = args->get(0).getAsIntegral();
    const char* name = index == 0 ? "first" : index == 1 ? "second" : nullptr;
    if (name == nullptr) return nullptr;
    for (const FieldDecl* f : rd->fields())
      if (f->getName() == name) return f;
    return nullptr;
  }

  std::optional<std::string> place(const Expr* e) {
    bool weak = false;
    return place(e, weak);
  }

  // Ett fält som platssteg: namnet för utskrift och klassen för identiteten
  // (Base::value och Derived::value är olika fält). core/Flow.cpp skriver
  // bara namnet, utom när två olika fält då skulle se likadana ut.
  static std::string fieldId(const FieldDecl& f) {
    return f.getNameAsString() + "@" + f.getParent()->getQualifiedNameAsString();
  }

  // Indexen som väljer platsen (v[i], v[i].f, en lokal referens bunden till
  // v[i]): de avgör vilket element som läses eller skrivs, så de är beroenden
  // vid både läsning och skrivning. Samma vandring som place().
  FlowValues indexOf(const Expr* e) {
    e = e->IgnoreParenImpCasts();
    if (const auto* d = dyn_cast<DeclRefExpr>(e); d != nullptr && bindingExpr(d) != nullptr)
      return indexOf(bindingExpr(d));
    if (const auto* d = dyn_cast<DeclRefExpr>(e)) {
      if (const auto* vd = dyn_cast<VarDecl>(d->getDecl()))
        if (auto a = aliasIndex_.find(vd); a != aliasIndex_.end()) return a->second;
      return {};
    }
    if (const auto* a = dyn_cast<ArraySubscriptExpr>(e)) {
      FlowValues v = indexOf(a->getBase());
      append(v, eval(a->getIdx()));
      return v;
    }
    if (const auto* op = dyn_cast<CXXOperatorCallExpr>(e);
        op != nullptr && op->getOperator() == OO_Subscript && op->getNumArgs() == 2) {
      FlowValues v = indexOf(op->getArg(0));
      append(v, eval(op->getArg(1)));
      return v;
    }
    if (const auto* m = dyn_cast<MemberExpr>(e); m != nullptr && !m->isArrow()) return indexOf(m->getBase());
    return {};
  }

  static void append(FlowValues& to, const FlowValues& from) { to.insert(to.end(), from.begin(), from.end()); }

  FlowValues eval(const Expr* e) {
    if (e == nullptr) return {};
    e = e->IgnoreParens();
    if (isa<LambdaExpr>(e)) {
      fail("lambda");
      return {};
    }
    // En lambda som värde (kopierad, flyttad, lagrad): vad den skriver genom
    // sina referensfångster syns inte i den platsen.
    if (const CXXRecordDecl* rd = e->getType().getNonReferenceType()->getAsCXXRecordDecl(); rd != nullptr && rd->isLambda()) {
      fail("lambda used as a value");
      return {};
    }
    if (isa<CXXThrowExpr>(e)) {
      fail("throw");
      return {};
    }
    if (const auto* d = dyn_cast<CXXDefaultArgExpr>(e)) return eval(d->getExpr());
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(e)) return eval(d->getExpr());
    if (const auto* op = dyn_cast<CXXOperatorCallExpr>(e); op != nullptr && op->getOperator() == OO_Subscript) {
      if (auto p = place(op)) {
        FlowValues v = indexOf(op);
        v.push_back(FlowValue::read(*p));
        return v;
      }
      FlowValues v = eval(op->getArg(1));
      append(v, eval(op->getArg(0)));
      return v;
    }
    if (const auto* a = dyn_cast<ArraySubscriptExpr>(e)) {
      if (auto p = place(a)) {
        FlowValues v = indexOf(a);
        v.push_back(FlowValue::read(*p));
        return v;
      }
      FlowValues v = eval(a->getIdx());
      append(v, eval(a->getBase()));
      return v;
    }
    if (isa<DeclRefExpr, MemberExpr>(e) || (isa<UnaryOperator>(e) && cast<UnaryOperator>(e)->getOpcode() == UO_Deref)) {
      if (auto p = place(e)) {
        FlowValues v = indexOf(e);
        v.push_back(FlowValue::read(*p));
        return v;
      }
    }
    if (const auto* d = dyn_cast<DeclRefExpr>(e)) {
      if (const Expr* bound = bindingExpr(d)) return eval(bound);
      if (const auto* vd = dyn_cast<VarDecl>(d->getDecl()); vd != nullptr && vd->hasLocalStorage())
        return {FlowValue::read(localName(vd))};  // en referens som inte är en känd plats: dess värde
      // Funktioner, enum-konstanter, konstanta globaler: inget att bero på.
      // Allt annat är okänt (tillåtelselista), inte "inget".
      if (isa<FunctionDecl, EnumConstantDecl, VarDecl>(d->getDecl())) return {};
      fail(std::string("unknown name ") + d->getDecl()->getDeclKindName());
      return {};
    }
    if (isa<CXXThisExpr>(e)) return {FlowValue::read("this")};
    if (const auto* c = dyn_cast<CallExpr>(e)) return call(c);
    if (const auto* c = dyn_cast<CXXConstructExpr>(e)) {
      FlowValues v;
      for (unsigned i = 0; i < c->getNumArgs(); ++i) {
        const ParmVarDecl* p = i < c->getConstructor()->getNumParams() ? c->getConstructor()->getParamDecl(i) : nullptr;
        if (p != nullptr && p->getType()->isReferenceType() && !p->getType()->getPointeeType().isConstQualified() &&
            !p->getType()->isRValueReferenceType()) {
          fail("constructor writes its argument");
          return {};
        }
        append(v, eval(c->getArg(i)));
      }
      return v;
    }
    if (const auto* b = dyn_cast<BinaryOperator>(e); b != nullptr && b->isAssignmentOp()) {
      fail("assignment inside an expression");
      return {};
    }
    if (const auto* u = dyn_cast<UnaryOperator>(e); u != nullptr && u->isIncrementDecrementOp()) {
      fail("increment inside an expression");
      return {};
    }
    FlowValues v;
    for (const Stmt* child : e->children())
      if (const auto* ce = dyn_cast_or_null<Expr>(child)) append(v, eval(ce));
    return v;
  }

  // En lambda direkt som argument (std::visit(f, v), std::ranges::sort(v, f)).
  static const LambdaExpr* lambdaArgument(const Expr* e) {
    for (;;) {
      e = e->IgnoreParens()->IgnoreImplicit();
      if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr();
      else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) e = b->getSubExpr();
      else break;
    }
    return dyn_cast<LambdaExpr>(e);
  }

  // Lambdan når omvärlden bara genom sina fångster (Stone: inga globaler):
  // fångster per värde läses, fångster per referens till en icke-const plats
  // kan också skrivas. Anropet blir okänd kod (allt den kan skriva beror på
  // allt den läser), också om den anropade har ett eget flöde.
  struct Closure {
    FlowValues reads;
    std::vector<std::string> writes;
  };

  // Lambdans fångster som läsningar och skrivningar. hidden: fångster per
  // värde sparas i en dold lokal (en lokal lambda anropas senare, med
  // värdena från när den skapades).
  std::optional<Closure> captures(const LambdaExpr& le, const std::string& hidden = "") {
    Closure c;
    FlowValues& reads = c.reads;
    std::vector<std::string>& writes = c.writes;
    FlowValues copies;
    auto init = le.capture_init_begin();
    for (const LambdaCapture& cap : le.captures()) {
      const Expr* e = init != le.capture_init_end() ? *init++ : nullptr;
      if (cap.capturesVariable())
        if (const auto* vd = dyn_cast<VarDecl>(cap.getCapturedVar())) {
          if (refLike(vd->getType())) {
            fail("lambda captures a pointer, iterator or view");
            return std::nullopt;
          }
          // Vad en annan lambda skriver syns inte i dess plats.
          const CXXRecordDecl* rd = vd->getType().getNonReferenceType()->getAsCXXRecordDecl();
          if (rd != nullptr && rd->isLambda()) {
            fail("lambda captures another lambda");
            return std::nullopt;
          }
        }
      if (cap.capturesThis()) {
        reads.push_back(FlowValue::read("this"));
        // [this]: metoden kan ändra *this genom lambdan om den inte är const.
        if (cap.getCaptureKind() == LCK_This && !g_.params.empty() && g_.params.front() == "this" && g_.outputs.front())
          writes.push_back("this");
        continue;
      }
      if (e == nullptr) {
        fail("lambda capture without an initializer");
        return std::nullopt;
      }
      if (cap.getCaptureKind() == LCK_ByRef) {
        auto p = place(e);
        if (!p) {
          fail("lambda captures by reference something that is not a known place");
          return std::nullopt;
        }
        append(reads, indexOf(e));
        reads.push_back(FlowValue::read(*p));
        if (!e->getType().isConstQualified()) writes.push_back(*p);
      } else {
        append(copies, eval(e));
      }
    }
    if (!g_.unsupported.empty()) return std::nullopt;
    if (!hidden.empty() && !copies.empty()) {
      block_->stmts.push_back({FlowStmt::Kind::assign, hidden, std::move(copies), {}});
      reads.push_back(FlowValue::read(hidden));
      // mutable: varje anrop kan ändra kopiorna, nästa anrop ser det.
      if (le.isMutable()) writes.push_back(hidden);
    } else {
      append(reads, copies);
    }
    return c;
  }

  // Anropet blir okänd kod med lambdans fångster som in- och utdata.
  static void addClosure(const Closure& c, FlowCall& fc) {
    fc.usr.clear();
    fc.args.push_back(c.reads);
    fc.refs.push_back("");
    for (const std::string& w : c.writes) {
      fc.args.push_back({FlowValue::read(w)});
      fc.refs.push_back(w);
    }
  }

  bool lambda(const LambdaExpr& le, FlowCall& fc) {
    auto c = captures(le);
    if (!c) return false;
    addClosure(*c, fc);
    return true;
  }

  // En lokal lambda (auto f = [...] {...}): dess fångster, eller nullptr.
  const Closure* closureOf(const Expr* e) const {
    for (;;) {  // en kopia som argument: f(less), less per värde
      e = e->IgnoreParenImpCasts();
      if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr();
      else if (const auto* c = dyn_cast<CXXConstructExpr>(e); c != nullptr && c->getNumArgs() == 1 &&
                                                              c->getConstructor()->isCopyOrMoveConstructor())
        e = c->getArg(0);
      else break;
    }
    const auto* d = dyn_cast<DeclRefExpr>(e);
    if (d == nullptr) return nullptr;
    auto it = closures_.find(dyn_cast<VarDecl>(d->getDecl()));
    return it == closures_.end() ? nullptr : &it->second;
  }

  FlowValues call(const CallExpr* c) {
    const FunctionDecl* callee = c->getDirectCallee();
    if (callee == nullptr) {
      fail("call through a pointer or a lambda");
      return {};
    }
    // Tilldelning av en klass med standardoperator=: en vanlig tilldelning.
    if (const auto* op = dyn_cast<CXXOperatorCallExpr>(c); op != nullptr && op->getOperator() == OO_Equal &&
                                                           (callee->isDefaulted() || callee->isTrivial())) {
      assign(op->getArg(0), eval(op->getArg(1)), false);
      return {};
    }
    FlowCall fc;
    fc.id = nextCall_++;
    fc.usr = usrOf(*callee);
    fc.name = callee->getQualifiedNameAsString();
    std::vector<const Expr*> args(c->arg_begin(), c->arg_end());
    const auto* md = dyn_cast<CXXMethodDecl>(callee);
    if (md != nullptr && md->isInstance()) {
      const Expr* object = nullptr;
      bool arrow = false;
      if (const auto* mc = dyn_cast<CXXMemberCallExpr>(c)) {
        object = mc->getImplicitObjectArgument();
        if (const auto* me = dyn_cast<MemberExpr>(mc->getCallee()->IgnoreParens())) arrow = me->isArrow();
      } else if (isa<CXXOperatorCallExpr>(c) && !args.empty()) {
        object = args.front();
        args.erase(args.begin());
      }
      if (object == nullptr) {
        fail("method call without an object");
        return {};
      }
      if (md->getParent()->isLambda()) {  // f(...) för en lokal lambda
        const Closure* cl = closureOf(object);
        if (cl == nullptr) {
          fail("call of a lambda that is not a local lambda");
          return {};
        }
        addClosure(*cl, fc);
        goto arguments;
      }
      {
      std::optional<std::string> p;
      if (arrow) {
        if (isa<CXXThisExpr>(object->IgnoreParenImpCasts())) p = "this";
      } else {
        p = place(object);
      }
      // Ett dispatchande anrop kan nå vilken override som helst: okänd kod
      // (konservativt), inte den statiskt valda metodens flöde.
      if (isDispatch(*c, *md, object)) fc.usr.clear();
      bool writes = !md->isConst();
      if (writes && !p) {
        fail("call of '" + fc.name + "' changes an object that is not a known place");
        return {};
      }
      fc.args.push_back(eval(object));
      fc.refs.push_back(writes ? *p : "");
      // v[i].set(x): indexet avgör vilket element metoden ändrar.
      if (writes)
        if (FlowValues ix = indexOf(object); !ix.empty()) {
          fc.refIndex.resize(fc.refs.size());
          fc.refIndex.back() = std::move(ix);
        }
      }
    }
  arguments:
    for (unsigned i = 0; i < args.size(); ++i) {
      if (const LambdaExpr* le = lambdaArgument(args[i])) {
        if (!lambda(*le, fc)) return {};
        continue;
      }
      if (const Closure* cl = closureOf(args[i])) {
        addClosure(*cl, fc);
        continue;
      }
      const ParmVarDecl* param = i < callee->getNumParams() ? callee->getParamDecl(i) : nullptr;
      QualType t = param != nullptr ? param->getType() : QualType();
      bool writable = !t.isNull() && t->isReferenceType() && !t->getPointeeType().isConstQualified();
      if (!t.isNull() && !writable && refLike(t)) {
        fail("call of '" + fc.name + "' can write through an iterator, pointer or view argument");
        return {};
      }
      if (writable) {
        auto p = place(args[i]);
        if (!p) {
          // En temporär bunden till T&& ändras utan att någon ser det.
          if (t->isRValueReferenceType() && !args[i]->IgnoreParenImpCasts()->isGLValue()) {
            fc.args.push_back(eval(args[i]));
            fc.refs.push_back("");
            continue;
          }
          fail("call of '" + fc.name + "' changes an argument that is not a known place");
          return {};
        }
        fc.args.push_back(eval(args[i]));
        fc.refs.push_back(*p);
        // out[i]: indexet avgör vilket element som skrivs (svag skrivning, Flow.cpp).
        if (FlowValues ix = indexOf(args[i]); !ix.empty()) {
          fc.refIndex.resize(fc.refs.size());
          fc.refIndex.back() = std::move(ix);
        }
      } else {
        fc.args.push_back(eval(args[i]));
        fc.refs.push_back("");
      }
    }
    unsigned id = fc.id;
    std::string note = isExpressionStatement(c) ? "the call of '" + fc.name + "' at " + lineOf(c) : "";
    block_->stmts.push_back({FlowStmt::Kind::call, {}, {}, std::move(fc), std::move(note)});
    return {FlowValue::result(id)};
  }

  void assign(const Expr* lhs, FlowValues values, bool readOld) {
    bool weak = false;
    auto p = place(lhs, weak);
    if (!p) {
      fail("assignment to something that is not a known place");
      return;
    }
    append(values, indexOf(lhs));
    if (readOld) values.push_back(FlowValue::read(*p));
    block_->stmts.push_back({weak ? FlowStmt::Kind::weak : FlowStmt::Kind::assign, *p, std::move(values), {},
                             "the assignment to '" + display(*p) + "' at " + lineOf(lhs)});
  }

  // Platsen för elementen i range-for: behållaren är en känd plats och en typ
  // vars element platsmodellen kallar c[*] (C-array, std::vector/array/deque).
  static const CXXForRangeStmt* rangeFor(const Stmt* s, const VarDecl* vd) {
    if (s == nullptr) return nullptr;
    if (const auto* fr = dyn_cast<CXXForRangeStmt>(s); fr != nullptr && fr->getLoopVariable() == vd) return fr;
    for (const Stmt* c : s->children())
      if (const CXXForRangeStmt* fr = rangeFor(c, vd)) return fr;
    return nullptr;
  }

  // Är vd pekaren __begin/__end i en range-for över en C-array?
  bool rangeIterator(const VarDecl* vd) const {
    if (!vd->isImplicit() || !vd->getType()->isPointerType()) return false;
    return rangeIteratorIn(fd_.getBody(), vd);
  }
  static bool rangeIteratorIn(const Stmt* s, const VarDecl* vd) {
    if (s == nullptr) return false;
    if (const auto* fr = dyn_cast<CXXForRangeStmt>(s))
      for (const DeclStmt* ds : {fr->getBeginStmt(), fr->getEndStmt()})
        if (ds != nullptr && ds->isSingleDecl() && ds->getSingleDecl() == vd) return knownRange(fr);
    for (const Stmt* c : s->children())
      if (rangeIteratorIn(c, vd)) return true;
    return false;
  }
  // Behållare vars element platsmodellen kallar c[*].
  static bool knownRange(const CXXForRangeStmt* fr) {
    QualType t = fr->getRangeInit()->getType().getNonReferenceType();
    if (t->isConstantArrayType()) return true;
    if (const auto* rd = t->getAsCXXRecordDecl()) {
      std::string name = rd->getQualifiedNameAsString();
      return name == "std::vector" || name == "std::array" || name == "std::deque";
    }
    return false;
  }

  bool constSpanRange(const VarDecl* vd) {
    const CXXForRangeStmt* fr = rangeFor(fd_.getBody(), vd);
    if (fr == nullptr) return false;
    const auto* d = dyn_cast<DeclRefExpr>(fr->getRangeInit()->IgnoreParenImpCasts());
    if (d == nullptr || !isa<ParmVarDecl>(d->getDecl())) return false;
    QualType t = d->getType().getNonReferenceType();
    const auto* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr || rd->getQualifiedNameAsString() != "std::span") return false;
    const auto* ts = dyn_cast<ClassTemplateSpecializationDecl>(rd);
    return ts != nullptr && ts->getTemplateArgs().size() > 0 &&
           ts->getTemplateArgs()[0].getKind() == TemplateArgument::Type &&
           ts->getTemplateArgs()[0].getAsType().isConstQualified();
  }

  std::optional<std::string> rangeElement(const VarDecl* vd) {
    const CXXForRangeStmt* fr = rangeFor(fd_.getBody(), vd);
    if (fr == nullptr) return std::nullopt;
    const Expr* r = fr->getRangeInit();
    if (!knownRange(fr)) return std::nullopt;
    if (auto p = place(r)) return *p + "[*]";
    return std::nullopt;
  }

  // Ett helt uttryck (en sats). En sats med temporärer ligger i en
  // ExprWithCleanups (en FullExpr), som inte är ett eget CFG-element: se
  // igenom den, annars försvinner satsen (v.push_back(1), o = W{...}).
  bool isRoot(const Stmt* s) const {
    const Stmt* parent = pm_->getParent(s);
    while (parent != nullptr && isa<FullExpr>(parent) && cast<FullExpr>(parent)->getSubExpr()->IgnoreParens() == s) {
      s = parent;
      parent = pm_->getParent(parent);
    }
    return parent == nullptr || !isa<Expr>(parent);
  }

  void element(const CFGElement& e, const Stmt* cond) {
    if (auto init = e.getAs<CFGInitializer>()) {
      const CXXCtorInitializer* i = init->getInitializer();
      if (!i->isMemberInitializer()) {
        fail("base or delegating initializer");
        return;
      }
      FlowValues v = eval(i->getInit());
      block_->stmts.push_back({FlowStmt::Kind::assign, "this." + fieldId(*i->getMember()), std::move(v), {}});
      return;
    }
    if (auto d = e.getAs<CFGAutomaticObjDtor>()) {
      if (hasUserDtor(d->getVarDecl()->getType())) fail("destructor with user code");
      return;
    }
    if (auto d = e.getAs<CFGTemporaryDtor>()) {
      if (hasUserDtor(d->getBindTemporaryExpr()->getType())) fail("destructor with user code");
      return;
    }
    auto cs = e.getAs<CFGStmt>();
    if (!cs) return;
    const Stmt* s = cs->getStmt();
    if (isa<CXXThrowExpr>(s)) {  // STN-83 (inom en ExprWithCleanups: inte en rot)
      throws_ = true;
      return;
    }
    if (const auto* se = dyn_cast<Expr>(s); s == cond || (se != nullptr && se->IgnoreParens() == cond)) return;
    if (!isRoot(s)) return;
    // Initieraren av en bindnings hållare (std::get på den dolda variabeln)
    // står som egen rot i CFG:n; hållaren är redan ett alias (holdingElement),
    // och get skriver ingenting.
    if (const auto* se = dyn_cast<Expr>(s); se != nullptr && getOnDecomposition(se) != nullptr) return;
    // PORK_PRE/PORK_POST är kontrakt, inget dataflöde (som Pre/Post i SPARK).
    if (const auto* c = dyn_cast<CallExpr>(s))
      if (const FunctionDecl* callee = c->getDirectCallee();
          callee != nullptr && isContractHelper(*callee))
        return;
    if (const auto* r = dyn_cast<ReturnStmt>(s)) {
      FlowValues v = eval(r->getRetValue());
      block_->stmts.push_back({FlowStmt::Kind::ret, {}, std::move(v), {}});
      return;
    }
    if (const auto* ds = dyn_cast<DeclStmt>(s)) {
      for (const Decl* d : ds->decls()) declare(d);
      return;
    }
    if (isa<CXXTryStmt, CXXCatchStmt, GotoStmt>(s)) {
      fail("try, catch or goto");
      return;
    }
    const auto* ex = dyn_cast<Expr>(s);
    if (ex == nullptr) return;
    ex = ex->IgnoreParens();
    if (const auto* fe = dyn_cast<FullExpr>(ex)) ex = fe->getSubExpr()->IgnoreParens();
    if (isa<CXXThrowExpr>(ex)) {  // STN-83: undantagsobjektet når ingen utdata
      throws_ = true;
      return;
    }
    if (const auto* b = dyn_cast<BinaryOperator>(ex); b != nullptr && b->isAssignmentOp()) {
      FlowValues v = eval(b->getRHS());
      assign(b->getLHS(), std::move(v), b->isCompoundAssignmentOp());
      return;
    }
    if (const auto* u = dyn_cast<UnaryOperator>(ex); u != nullptr && u->isIncrementDecrementOp()) {
      assign(u->getSubExpr(), {}, true);
      return;
    }
    // Initieraren av en lokal lambda är ett eget element under sin DeclStmt:
    // declare() tar hand om fångsterna.
    if (const LambdaExpr* le = lambdaArgument(ex)) {
      if (const auto* ds = dyn_cast_or_null<DeclStmt>(pm_->getParent(le)); ds != nullptr) return;
    }
    eval(ex);  // anrop som sats
  }

  // BRZ-7: "at line N" för anteckningarna.
  std::string lineOf(const Stmt* s) const {
    const SourceManager& sm = fd_.getASTContext().getSourceManager();
    return "line " + std::to_string(sm.getExpansionLineNumber(s->getBeginLoc()));
  }

  static std::string display(const std::string& p) {
    std::string out;
    bool skip = false;
    for (char c : p) {
      if (c == '@' || c == '~') skip = true;  // fältets klass, lokalens nummer
      else if (c == '.' || c == '[') skip = false;
      if (!skip) out += c;
    }
    return out;
  }

  // Är anropet hela satsen (`f(x);`, `(void)f(x);`, ett for-steg), så att
  // resultatet kastas? Inte i return, en initierare eller ett större uttryck.
  bool isExpressionStatement(const Expr* e) const {
    const Stmt* child = e;
    const Stmt* up = pm_->getParent(e);
    while (up != nullptr && isa<ParenExpr, FullExpr, CastExpr, CXXBindTemporaryExpr, MaterializeTemporaryExpr>(up)) {
      child = up;
      up = pm_->getParent(up);
    }
    if (up == nullptr) return false;
    if (isa<CompoundStmt, CaseStmt, DefaultStmt, AttributedStmt>(up)) return true;
    if (const auto* i = dyn_cast<IfStmt>(up)) return child == i->getThen() || child == i->getElse();
    if (const auto* w = dyn_cast<WhileStmt>(up)) return child == w->getBody();
    if (const auto* d = dyn_cast<DoStmt>(up)) return child == d->getBody();
    if (const auto* f = dyn_cast<ForStmt>(up)) return child == f->getBody() || child == f->getInc();
    if (const auto* f = dyn_cast<CXXForRangeStmt>(up)) return child == f->getBody();
    return false;
  }

  void declare(const Decl* d) {
    const auto* vd = dyn_cast<VarDecl>(d);
    if (vd == nullptr) return;
    if (!vd->hasLocalStorage()) {
      // STN-75: en statisk lokal konstant (constexpr, eller const med
      // konstant initiering) är en tabell som aldrig skrivs; läsningar av den
      // beror bara på indexet (eval ger inget för icke-lokala VarDecl).
      const VarDecl* init = nullptr;
      bool constant = vd->isConstexpr() ||
                      (vd->getType().isConstant(vd->getASTContext()) && vd->getAnyInitializer(init) != nullptr &&
                       init->hasConstantInitialization());
      if (!constant) fail("static local");
      return;
    }
    const Expr* init = vd->getInit();
    if (const LambdaExpr* le = init != nullptr ? lambdaArgument(init) : nullptr) {
      auto c = captures(*le, localName(vd) + ".captures");
      if (c) closures_[vd] = std::move(*c);
      return;
    }
    if (vd->getType()->isReferenceType()) {
      // for (const T& e : c): e är ett element i c (svagt, vilket som helst).
      if (vd->isCXXForRangeDecl()) {
        if (auto p = rangeElement(vd)) {
          aliases_[vd] = *p;
          aliasIndex_.erase(vd);
          return;
        }
        // Över en std::span<const T>-parameter (STN, constSpanSpec): elementen
        // kan inte ändras under funktionen, så värdet vid bindningen räcker.
        if (!constSpanRange(vd)) {
          fail("range-for over something that is not a known container");
          return;
        }
        FlowValues v = eval(init);
        block_->stmts.push_back({FlowStmt::Kind::assign, localName(vd), std::move(v), {}});
        return;
      }
      if (auto p = holdingElement(vd)) {
        aliases_[vd] = *p;
        aliasIndex_.erase(vd);
        return;
      }
      if (init != nullptr)
        if (auto p = place(init)) {
          aliases_[vd] = *p;
          // const int& e = v[i]: indexet vid bindningen väljer elementet. En
          // dold lokal håller värdet, så att en senare ändring av i inte räknas.
          if (FlowValues ix = indexOf(init); !ix.empty()) {
            std::string hidden = localName(vd) + ".index";
            block_->stmts.push_back({FlowStmt::Kind::assign, hidden, std::move(ix), {}});
            aliasIndex_[vd] = {FlowValue::read(hidden)};
          } else {
            aliasIndex_.erase(vd);
          }
          return;
        }
      // En referens som ett anrop returnerar (box.get()) pekar in i ett objekt
      // som kan ändras medan referensen lever; vilken plats vet vi inte än.
      // Bara en temporär (const T& r = f() med värderesultat) är en kopia.
      if (init != nullptr) {
        const Expr* e = init->IgnoreParenImpCasts();
        if (const auto* fe = dyn_cast<FullExpr>(e)) e = fe->getSubExpr()->IgnoreParenImpCasts();
        if (!isa<MaterializeTemporaryExpr>(e) && e->isGLValue()) {
          fail("reference bound to a reference returned by a call");
          return;
        }
      }
      // Inte en känd plats (const T& till ett temporärt resultat): ett värde
      // att läsa, aldrig ett mål (place() ger nullopt).
      FlowValues v = eval(init);
      block_->stmts.push_back({FlowStmt::Kind::assign, localName(vd), std::move(v), {}, initNote(*vd)});
      return;
    }
    if (init == nullptr) return;  // oinitierad (Stone avvisar läsning före tilldelning)
    FlowValues v = eval(init);
    block_->stmts.push_back({FlowStmt::Kind::assign, localName(vd), std::move(v), {}, initNote(*vd)});
  }

  // En loopvariabel i range-for behöver inte användas (`for (auto x : v) ++n;`).
  std::string initNote(const VarDecl& vd) const {
    if (vd.isCXXForRangeDecl() || vd.isImplicit() || isa<DecompositionDecl>(vd)) return "";
    return "the initialisation of '" + vd.getNameAsString() + "' at " + lineOf(&*vd.getInit());
  }

  // Kör förstörelsen av t egen kod? En implicit eller = default-destruktor
  // förstör bara baser och fält: följ dem (World med std::vector är inte
  // egen kod). En destruktor med egen kropp utanför systemheaders är det.
  static bool hasUserDtor(QualType t) {
    std::set<const CXXRecordDecl*> seen;
    return hasUserDtor(t, seen);
  }
  static bool hasUserDtor(QualType t, std::set<const CXXRecordDecl*>& seen) {
    t = t.getNonReferenceType();
    while (const ArrayType* at = t->getAsArrayTypeUnsafe()) t = at->getElementType();
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition()) return false;
    rd = rd->getDefinition();
    if (!seen.insert(rd).second) return false;
    const CXXDestructorDecl* dtor = rd->getDestructor();
    if (dtor == nullptr || dtor->isTrivial()) return false;
    // En mall i standardbiblioteket (std::optional<T>, std::vector<T>,
    // std::variant<A, B>) förstör värden av sina typargument: följ dem.
    if (fd_inSystem(dtor)) {
      if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(rd))
        for (const TemplateArgument& a : spec->getTemplateArgs().asArray())
          if (typeArgumentHasUserDtor(a, seen)) return true;
      return false;
    }
    if (!dtor->isDefaulted()) return true;
    for (const CXXBaseSpecifier& b : rd->bases())
      if (hasUserDtor(b.getType(), seen)) return true;
    for (const CXXBaseSpecifier& b : rd->vbases())
      if (hasUserDtor(b.getType(), seen)) return true;
    for (const FieldDecl* f : rd->fields())
      if (hasUserDtor(f->getType(), seen)) return true;
    return false;
  }
  static bool typeArgumentHasUserDtor(const TemplateArgument& a, std::set<const CXXRecordDecl*>& seen) {
    if (a.getKind() == TemplateArgument::Type) return hasUserDtor(a.getAsType(), seen);
    if (a.getKind() == TemplateArgument::Pack)
      for (const TemplateArgument& p : a.pack_elements())
        if (typeArgumentHasUserDtor(p, seen)) return true;
    return false;
  }
  static bool fd_inSystem(const FunctionDecl* f) {
    return f->getASTContext().getSourceManager().isInSystemHeader(f->getLocation());
  }

  const FunctionDecl& fd_;
  FlowGraph g_;
  std::unique_ptr<ParentMap> pm_;
  FlowBlock* block_ = nullptr;
  std::map<const VarDecl*, std::string> locals_;
  std::map<const VarDecl*, std::string> aliases_;
  std::map<const VarDecl*, FlowValues> aliasIndex_;
  std::map<const VarDecl*, Closure> closures_;  // lokala lambdor  // indexet som valde aliasets element
  unsigned nextCall_ = 1;
  bool throws_ = false;  // blocket slutar i ett kast
};

}  // namespace

FlowGraph buildFlowGraph(const FunctionDecl& fd) { return Builder(fd).run(); }

}  // namespace pork
