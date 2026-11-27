#include "LocalCheck.hpp"

#include "Assert.hpp"
#include "Dispatch.hpp"
#include "HintAttributes.hpp"
#include "EnumSwitch.hpp"
#include "LoanCheck.hpp"
#include "Move.hpp"

#include "Span.hpp"
#include "StringLiteralArg.hpp"
#include "VectorBool.hpp"

#include <clang/AST/Attr.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Analysis/Analyses/LiveVariables.h>
#include <clang/Analysis/Analyses/UninitializedValues.h>
#include <clang/Analysis/AnalysisDeclContext.h>
#include <clang/Analysis/CFG.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/SourceManager.h>
#include <clang/UnifiedSymbolResolution/USRGeneration.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/raw_ostream.h>

#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using namespace clang;

namespace pork {

std::string usrOf(const Decl& d) {
  llvm::SmallString<128> buf;
  if (index::generateUSRForDecl(&d, buf)) return {};  // true = misslyckades
  return std::string(buf);
}

std::string displayName(const FunctionDecl& fd) {
  std::string out;
  llvm::raw_string_ostream os(out);
  fd.getNameForDiagnostic(os, fd.getASTContext().getPrintingPolicy(), /*Qualified=*/true);
  return out;
}

bool hasAnnotation(const FunctionDecl& fd, std::string_view annotation) {
  auto annotated = [&](const FunctionDecl& d) {
    for (const FunctionDecl* r : d.redecls())
      for (const auto* a : r->specific_attrs<AnnotateAttr>())
        if (std::string_view(a->getAnnotation()) == annotation) return true;
    return false;
  };
  if (annotated(fd)) return true;
  const FunctionDecl* pattern = fd.getTemplateInstantiationPattern();
  return pattern != nullptr && annotated(*pattern);
}

bool isContractHelper(const FunctionDecl& fd) {
  const FunctionDecl* p = fd.getPrimaryTemplate() != nullptr ? fd.getPrimaryTemplate()->getTemplatedDecl() : &fd;
  for (const char* a : {"pork::pre", "pork::post", "pork::depends", "pork::depends_result"})
    if (hasAnnotation(*p, a) || hasAnnotation(fd, a)) return true;
  return false;
}

const CallExpr* flowRequirement(const Stmt* s, bool* result) {
  const auto* e = dyn_cast_or_null<Expr>(s);
  if (e == nullptr) return nullptr;
  e = e->IgnoreParens();
  if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr()->IgnoreParens();
  const auto* c = dyn_cast<CallExpr>(e);
  if (c == nullptr) return nullptr;
  const FunctionDecl* callee = c->getDirectCallee();
  if (callee == nullptr) return nullptr;
  const FunctionDecl* p = callee->getPrimaryTemplate() != nullptr ? callee->getPrimaryTemplate()->getTemplatedDecl() : callee;
  bool r = hasAnnotation(*p, "pork::depends_result");
  if (!r && !hasAnnotation(*p, "pork::depends")) return nullptr;
  if (result != nullptr) *result = r;
  return c;
}


namespace {
// std::unique_ptr<T, D> (STN-69, STN-90).
bool isUniquePtrType(QualType t) {
  const auto* rd = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
  return rd != nullptr && rd->getSpecializedTemplate()->getQualifiedNameAsString() == "std::unique_ptr";
}

// STN-88: std-mallar med delat ägande (libstdc++ har baserna __shared_ptr och __weak_ptr).
bool sharedOwnership(const std::string& tmpl) {
  return tmpl == "std::shared_ptr" || tmpl == "std::__shared_ptr" || tmpl == "std::weak_ptr" ||
         tmpl == "std::__weak_ptr" || tmpl == "std::enable_shared_from_this";
}

// Den anropade (eller, före instansiering, någon kandidat i uppslagningen)
// bär en av annoteringarna.
bool calls(const CallExpr& c, std::initializer_list<const char*> annotations) {
  auto annotated = [&](const FunctionDecl& fd) {
    const FunctionDecl* p = fd.getPrimaryTemplate() != nullptr ? fd.getPrimaryTemplate()->getTemplatedDecl() : &fd;
    return std::ranges::any_of(annotations, [&](const char* a) { return hasAnnotation(*p, a); });
  };
  if (const FunctionDecl* fd = c.getDirectCallee()) return annotated(*fd);
  const auto* u = dyn_cast<UnresolvedLookupExpr>(c.getCallee()->IgnoreParenImpCasts());
  if (u == nullptr) return false;
  for (const NamedDecl* d : u->decls()) {
    d = d->getUnderlyingDecl();
    if (const auto* t = dyn_cast<FunctionTemplateDecl>(d)) d = t->getTemplatedDecl();
    if (const auto* fd = dyn_cast<FunctionDecl>(d); fd != nullptr && annotated(*fd)) return true;
  }
  return false;
}

const CallExpr* statementCall(const Stmt* s) {
  const auto* e = dyn_cast_or_null<Expr>(s);
  if (e == nullptr) return nullptr;
  e = e->IgnoreParens();
  if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr()->IgnoreParens();
  return dyn_cast<CallExpr>(e);
}
}  // namespace

bool isRequirementCall(const CallExpr& c) { return calls(c, {"pork::depends", "pork::depends_result"}); }

std::vector<const CallExpr*> leadingRequirements(const FunctionDecl& fd) {
  std::vector<const CallExpr*> out;
  const auto* body = dyn_cast_or_null<CompoundStmt>(fd.getBody());
  if (body == nullptr) return out;
  const ASTContext& ctx = fd.getASTContext();
  // Kontraktsanropen känns igen också i ett mallmönster (olöst uppslagning).
  for (const Stmt* s : body->body()) {
    const CallExpr* c = statementCall(s);
    if (c != nullptr && isRequirementCall(*c)) {
      out.push_back(c);
      continue;
    }
    if ((c != nullptr && calls(*c, {"pork::pre", "pork::post"})) ||
        assertMacro(s, ctx.getSourceManager(), ctx.getLangOpts()) ||
        disabledAssert(s, ctx.getSourceManager(), ctx.getLangOpts()) ||
        (fd.isTemplated() && wholeAssertExpansion(s, ctx.getSourceManager(), ctx.getLangOpts())))
      continue;
    break;
  }
  return out;
}

namespace {
class RequirementFinder : public RecursiveASTVisitor<RequirementFinder> {
 public:
  std::vector<const CallExpr*> found;
  bool VisitCallExpr(CallExpr* c) {
    if (isRequirementCall(*c)) found.push_back(c);
    return true;
  }
  // En lokal klass är egna funktioner med egna krav (analyseras för sig).
  bool TraverseCXXRecordDecl(CXXRecordDecl* rd) {
    return rd->isLambda() ? RecursiveASTVisitor::TraverseCXXRecordDecl(rd) : true;
  }
  // Ett defaultargument står utanför kroppen: varken funktionen som
  // deklarerar det eller anroparen som använder standardvärdet äger det
  // (ClangFactsSource rapporterar det där det står).
  bool TraverseCXXDefaultArgExpr(CXXDefaultArgExpr*) { return true; }
  // Samma för en fältinitierare som en konstruktor använder (står vid fältet).
  bool TraverseCXXDefaultInitExpr(CXXDefaultInitExpr*) { return true; }
  bool TraverseParmVarDecl(ParmVarDecl*) { return true; }
  bool shouldVisitLambdaBody() const { return true; }
  bool shouldVisitImplicitCode() const { return true; }
};
}  // namespace

std::vector<const CallExpr*> allRequirements(const FunctionDecl& fd) {
  RequirementFinder finder;
  finder.TraverseDecl(const_cast<FunctionDecl*>(&fd));
  return finder.found;
}

// PORK_PRE(c) när pork analyserar: ett anrop till pork::pre(bool).
const Expr* preCondition(const Stmt* s) {
  const auto* e = dyn_cast_or_null<Expr>(s);
  if (e == nullptr) return nullptr;
  e = e->IgnoreParens();
  if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr()->IgnoreParens();
  const auto* c = dyn_cast<CallExpr>(e);
  if (c == nullptr || c->getNumArgs() != 1) return nullptr;
  const FunctionDecl* callee = c->getDirectCallee();
  if (callee == nullptr || !hasAnnotation(*callee, "pork::pre")) return nullptr;
  return c->getArg(0);
}

// PORK_POST(r, c) när pork analyserar: pork::post([&](R r) -> bool { return c; }).
const LambdaExpr* postCondition(const Stmt* s) {
  const auto* e = dyn_cast_or_null<Expr>(s);
  if (e == nullptr) return nullptr;
  e = e->IgnoreParens();
  if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr()->IgnoreParens();
  const auto* c = dyn_cast<CallExpr>(e);
  if (c == nullptr || c->getNumArgs() != 1) return nullptr;
  const FunctionDecl* callee = c->getDirectCallee();
  if (callee == nullptr || !hasAnnotation(*callee, "pork::post")) return nullptr;
  const Expr* arg = c->getArg(0)->IgnoreImplicit();
  if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(arg)) arg = m->getSubExpr()->IgnoreImplicit();
  return dyn_cast<LambdaExpr>(arg);
}

const Expr* postConditionExpr(const LambdaExpr& le) {
  const auto* body = dyn_cast_or_null<CompoundStmt>(le.getCallOperator()->getBody());
  if (body == nullptr || body->size() != 1) return nullptr;
  const auto* r = dyn_cast<ReturnStmt>(body->body_front());
  return r != nullptr ? r->getRetValue() : nullptr;
}

// Namnet som matchas mot trust-listan: kvalificerat, utan templateargument.
// Medlemmar i en klasstemplate-instans får mallens namn:
// std::optional<int>::has_value -> "std::optional::has_value".
// En C-funktion i det globala namnrummet (glibc:s ::sqrt) som std exporterar
// med en using-deklaration (`namespace std { using ::sqrt; }` i <cmath>).
static bool reexportedByStd(const FunctionDecl& fd) {
  if (!fd.getDeclContext()->getRedeclContext()->isTranslationUnit() || fd.getDeclName().isEmpty()) return false;
  ASTContext& ctx = fd.getASTContext();
  if (!ctx.getSourceManager().isInSystemHeader(fd.getLocation())) return false;
  for (const NamedDecl* d : ctx.getTranslationUnitDecl()->lookup(&ctx.Idents.get("std"))) {
    const auto* ns = dyn_cast<NamespaceDecl>(d);
    if (ns == nullptr) continue;
    for (const NamedDecl* m : ns->getPrimaryContext()->lookup(fd.getDeclName()))
      if (const auto* shadow = dyn_cast<UsingShadowDecl>(m);
          shadow != nullptr && shadow->getTargetDecl()->getCanonicalDecl() == fd.getCanonicalDecl())
        return true;
  }
  return false;
}

std::string trustAlias(const FunctionDecl& fd) {
  return reexportedByStd(fd) ? fd.getQualifiedNameAsString() : std::string();
}

std::string trustName(const FunctionDecl& fd) {
  if (reexportedByStd(fd)) return "std::" + fd.getNameAsString();
  if (const auto* md = dyn_cast<CXXMethodDecl>(&fd)) {
    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(md->getParent())) {
      const ClassTemplateDecl* tmpl = spec->getSpecializedTemplate();
      std::string member = isa<CXXConstructorDecl>(md)  ? tmpl->getNameAsString()
                           : isa<CXXDestructorDecl>(md) ? "~" + tmpl->getNameAsString()
                                                        : md->getNameAsString();
      return tmpl->getQualifiedNameAsString() + "::" + member;
    }
  }
  return fd.getQualifiedNameAsString();
}

// Kvalificerat namn med templateargument, t.ex. "Box<Holder>".
std::string recordName(const CXXRecordDecl& rd) {
  std::string out;
  llvm::raw_string_ostream os(out);
  rd.getNameForDiagnostic(os, rd.getASTContext().getPrintingPolicy(), /*Qualified=*/true);
  return out;
}

namespace {

// Ett delobjekt: en direkt bas eller ett fält, i initieringsordning (P-c i
// docs/stone_model.md). Allt som handlar om initiering, värdetyper,
// destruktorer och `hasFields` går genom subobjects(): en ny sorts delobjekt
// blir en ändring på ett ställe.
struct Subobject {
  const CXXBaseSpecifier* base = nullptr;  // eller
  const FieldDecl* field = nullptr;
  QualType type() const { return base != nullptr ? base->getType() : field->getType(); }
};
std::vector<Subobject> subobjects(const CXXRecordDecl& rd);
bool hasFields(QualType t);
std::optional<std::string> repeatedBase(const CXXRecordDecl& rd);

// ---------------------------------------------------------------------------
// Typregler: vad är en värdetyp?
// ---------------------------------------------------------------------------

// STN-67: egen kod som en funktion i en systemheader kan köra. Följer de
// instansierade kropparna (och implicita specialmedlemmar) tills den når
// funktioner som användaren skrivit; de är anrop som Stone kräver.
class UserCodeScan {
 public:
  explicit UserCodeScan(const SourceManager& sm) : sm_(sm) {}

  std::vector<const FunctionDecl*> from(const FunctionDecl& f) {
    out_.clear();
    function(f);
    return out_;
  }

 private:
  void function(const FunctionDecl& f) {
    if (!seen_.insert(f.getCanonicalDecl()).second) return;
    const auto* md = dyn_cast<CXXMethodDecl>(&f);
    // Lambdans operator() granskas där den skrivs (STN-40); closure-typens
    // destruktor och kopiering följs (fångsterna).
    if (md != nullptr && md->getParent()->isLambda() && !isa<CXXDestructorDecl, CXXConstructorDecl>(md)) return;
    bool system = sm_.isInSystemHeader(f.getLocation());
    if (!system && (md == nullptr || md->isUserProvided())) {
      out_.push_back(&f);
      return;
    }
    const FunctionDecl* def = f.getDefinition();
    if (const auto* ctor = dyn_cast<CXXConstructorDecl>(def != nullptr ? def : &f))
      for (const CXXCtorInitializer* init : ctor->inits()) walk(init->getInit());
    if (const auto* dtor = dyn_cast<CXXDestructorDecl>(&f)) members(*dtor->getParent());
    if (def != nullptr) walk(def->getBody());
  }

  // Fält och baser förstörs efter destruktorns kropp.
  void members(const CXXRecordDecl& rd) {
    if (!rd.hasDefinition()) return;
    for (const Subobject& o : subobjects(rd)) destroy(o.type());
  }

  void destroy(QualType t) {
    if (t.isNull() || t->isReferenceType()) return;
    t = ctx(t);
    if (const CXXRecordDecl* rd = t->getAsCXXRecordDecl(); rd != nullptr && rd->hasDefinition())
      if (const CXXDestructorDecl* d = rd->getDefinition()->getDestructor()) function(*d);
  }

  static QualType ctx(QualType t) {
    t = t.getCanonicalType();
    while (const auto* a = dyn_cast<ArrayType>(t.getTypePtr())) t = a->getElementType().getCanonicalType();
    return t;
  }

  void walk(const Stmt* s) {
    if (s == nullptr) return;
    if (const auto* c = dyn_cast<CallExpr>(s)) {
      if (const FunctionDecl* f = c->getDirectCallee()) function(*f);
    } else if (const auto* c = dyn_cast<CXXConstructExpr>(s)) {
      function(*c->getConstructor());
    } else if (const auto* c = dyn_cast<CXXInheritedCtorInitExpr>(s)) {
      // En ärvd konstruktor (`using Base::Base;`) anropar basens: std::optional
      // kopierar sitt värde så (_Optional_payload ärver _Optional_payload_base).
      function(*c->getConstructor());
    } else if (const auto* d = dyn_cast<CXXDeleteExpr>(s)) {
      destroy(d->getDestroyedType());
    } else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(s)) {
      if (const CXXDestructorDecl* d = b->getTemporary()->getDestructor()) function(*d);
    } else if (const auto* d = dyn_cast<CXXDefaultArgExpr>(s)) {
      walk(d->getExpr());
    } else if (const auto* d = dyn_cast<CXXDefaultInitExpr>(s)) {
      walk(d->getExpr());
    } else if (const auto* l = dyn_cast<LambdaExpr>(s)) {
      walk(l->getBody());
    } else if (const auto* ds = dyn_cast<DeclStmt>(s)) {
      for (const Decl* d : ds->decls())
        if (const auto* vd = dyn_cast<VarDecl>(d); vd != nullptr && vd->hasLocalStorage()) destroy(vd->getType());
    }
    for (const Stmt* c : s->children()) walk(c);
  }

  const SourceManager& sm_;
  std::set<const FunctionDecl*> seen_;
  std::vector<const FunctionDecl*> out_;
};

class TypeRules {
 public:
  TypeRules(const ASTContext& ctx, const TrustList& trust, std::set<std::string>& assumed,
            const LoanSummaries* summaries = nullptr)
      : ctx_(ctx), policy_(ctx.getPrintingPolicy()), trust_(trust), assumed_(assumed), summaries_(summaries) {
    policy_.SuppressTagKeyword = true;  // "Holder", inte "struct Holder"
  }

  std::string typeName(QualType t) const { return t.getAsString(policy_); }

  // nullopt = värdetyp. Annars en (ev. tom) förklaring som läggs efter typnamnet.
  //
  // Värdetyper: heltal (inkl. bool och char), flyttal, enum, structar enligt
  // recordReason och betrodda typer. Inte volatile, pekare, referenser, arrayer.
  std::optional<std::string> whyNotValue(QualType t) {
    if (t.isNull()) return "";
    QualType c = t.getCanonicalType();
    if (c.isVolatileQualified()) return "";
    if (const auto* b = dyn_cast<BuiltinType>(c.getTypePtr()))
      return (b->isInteger() || b->isFloatingPoint()) ? std::nullopt : std::optional<std::string>("");
    if (c->isEnumeralType()) return std::nullopt;
    if (const CXXRecordDecl* rd = c->getAsCXXRecordDecl()) {
      if (auto r = recordReason(*rd)) return " (" + *r + ")";
      return std::nullopt;
    }
    return "";
  }

  bool isValueType(QualType t) { return !whyNotValue(t); }

  // Lånar resultatet av ett std-anrop bara objektet (eller första argumentet
  // för en fri funktion)? Enligt std-sammanfattningarna (summaries/std.txt):
  // effekterna access, writeAccess och insert. Utan sammanfattningar: nej.
  // STN-74: tar anropet en position eller ett intervall (summaries/std.txt)?
  std::pair<bool, bool> positionArgs(const FunctionDecl& fd) const {
    if (summaries_ == nullptr) return {false, false};
    LoanRule rule = libraryRule(*summaries_, &fd);
    return {rule.position, rule.range};
  }

  // STN-37/78: std.txt säger att anropet inte skriver (reads, access). En
  // saknad rad = okänt = skriver (strängare, aldrig osunt).
  bool libraryReads(const FunctionDecl& fd) const {
    if (summaries_ == nullptr) return false;
    LoanRule rule = libraryRule(*summaries_, &fd);
    return !rule.origin.empty() && (rule.effect == LoanEffect::reads || rule.effect == LoanEffect::access);
  }

  // STN-101: std.txt säger att anropet ger objektet ett bestämt värde (clear).
  bool libraryResets(const FunctionDecl& fd) const {
    if (summaries_ == nullptr) return false;
    LoanRule rule = libraryRule(*summaries_, &fd);
    return !rule.origin.empty() && rule.resets;
  }

  bool borrowsOnlyFirst(const FunctionDecl& fd) const {
    if (summaries_ == nullptr) return false;
    LoanEffect e = libraryRule(*summaries_, &fd).effect;
    return e == LoanEffect::access || e == LoanEffect::writeAccess || e == LoanEffect::insert;
  }

  // STN-75: en ägd array, inbäddad lagring med storlek känd vid
  // kompileringen. Elementen är värden utan egen kod vid kopiering och
  // destruktion (skalär, enum, trivialt kopierbar värdestruct) eller själva
  // ägda arrayer. Arrayen som helhet är fortfarande ingen värdetyp (den kan
  // inte kopieras eller returneras); se Checker::ownedArrayBase för var den
  // får användas. nullopt = ägd array, annars en förklaring efter typnamnet.
  std::optional<std::string> whyNotOwnedArray(QualType t) {
    const auto* array = ctx_.getAsConstantArrayType(t);
    if (array == nullptr) return "";
    if (array->getSize().isZero()) return " (array of length 0)";
    QualType element = array->getElementType();
    if (element->isConstantArrayType()) return whyNotOwnedArray(element);
    if (auto w = whyNotValue(element)) return " (element type " + typeName(element) + *w + ")";
    if (!element.isTriviallyCopyableType(ctx_))
      return " (element type " + typeName(element) + " is not trivially copyable)";
    return std::nullopt;
  }

  bool isOwnedArray(QualType t) { return t->isConstantArrayType() && !whyNotOwnedArray(t); }

  // Fältets typ som förklaring: en array prövas som ägd array.
  std::optional<std::string> whyNotFieldType(QualType t) {
    return t->isConstantArrayType() ? whyNotOwnedArray(t) : whyNotValue(t);
  }

  // Lånevärde: en typ som vore en värdetyp om den inte bar lån. Lånen
  // kontrolleras av lånemodellen (STN-62, alltid på), så reglerna behandlar
  // lånevärdet som ett värde:
  //  - pekare till en objekttyp,
  //  - std-typer som clang märker [[gsl::Pointer]] (iteratorer, span, string_view),
  //  - klassmallar i std som inte äger ([[gsl::Owner]]) och är betrodda eller
  //    har trivial destruktor (std::pair), med värden eller lånevärden som
  //    typargument och minst ett lånevärde.
  // Inte en behållare av lånevärden (vector<int*>): modellen följer inte lån
  // som lagras via anrop (push_back(&x)), så typregeln behövs där.
  bool isLoanValue(QualType t) {
    if (t.isNull()) return false;
    QualType c = t.getCanonicalType();
    if (c.isVolatileQualified() || c->isReferenceType()) return false;
    // STN-71: std::initializer_list<T> pekar på en temporär array; ett
    // lånevärde av en värdetyp T, oavsett trust-fil. T får själv vara en
    // sådan lista: de inre arrayerna lever lika länge som den yttre.
    if (auto element = initializerListElement(c))
      return isValueType(*element) || (initializerListElement(*element) && isLoanValue(*element));
    if (c->isPointerType()) {
      QualType p = c->getPointeeType();
      return !p->isFunctionType() && !p->isVoidType() && !whyNotObject(p);
    }
    const CXXRecordDecl* rd = c->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition()) return false;
    if (!ctx_.getSourceManager().isInSystemHeader(rd->getLocation())) return false;
    if (isGslPointerRecord(*rd)) return true;
    const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(rd);
    if (spec == nullptr || isGslOwnerRecord(*rd)) return false;
    // Betrodd, eller utan något att äga: trivial destruktor (std::pair av en
    // iterator och en bool).
    std::string tmpl = spec->getSpecializedTemplate()->getQualifiedNameAsString();
    if (sharedOwnership(tmpl)) return false;  // STN-88
    bool trusted = trust_.types.contains(tmpl);
    if (!trusted && !rd->getDefinition()->hasTrivialDestructor()) return false;
    bool loan = false;
    for (const TemplateArgument& a : spec->getTemplateArgs().asArray()) {
      if (a.getKind() != TemplateArgument::Type) continue;
      if (isValueType(a.getAsType())) continue;
      if (!isLoanValue(a.getAsType())) return false;
      loan = true;
    }
    if (loan && trusted) assumed_.insert("type " + tmpl);
    return loan;
  }

  // Värde eller lånevärde.
  bool isValueOrLoan(QualType t) { return isValueType(t) || isLoanValue(t); }

  // STN-71: elementet i en initializer_list-parameter: en värdetyp, eller en
  // initializer_list av sådana (nästlade listor).
  std::optional<std::string> whyNotListElement(QualType t) {
    if (auto inner = initializerListElement(t)) return whyNotListElement(*inner);
    return whyNotValue(t);
  }

  // Elementtypen T om t är std::initializer_list<T>.
  static std::optional<QualType> initializerListElement(QualType t) {
    const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
    if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::initializer_list")
      return std::nullopt;
    return spec->getTemplateArgs()[0].getAsType();
  }

  static bool isGslPointerRecord(const CXXRecordDecl& rd) {
    if (rd.hasAttr<PointerAttr>()) return true;
    const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(&rd);
    return spec != nullptr && spec->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<PointerAttr>();
  }
  static bool isGslOwnerRecord(const CXXRecordDecl& rd) {
    if (rd.hasAttr<OwnerAttr>()) return true;
    const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(&rd);
    return spec != nullptr && spec->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<OwnerAttr>();
  }

  // Gränssnitt: en klass utan datafält, vars baser också är gränssnitt, med
  // minst en virtuell funktion. En referens till ett gränssnitt är en port:
  // det finns inget i den som analysen kan se eller ändra direkt.
  bool isInterface(const CXXRecordDecl& decl) const {
    const CXXRecordDecl* rd = decl.getDefinition();
    if (rd == nullptr || !rd->isPolymorphic() || rd->isUnion() || rd->getNumVBases() != 0) return false;
    if (!rd->fields().empty()) return false;
    for (const CXXBaseSpecifier& b : rd->bases()) {
      const CXXRecordDecl* base = b.getType()->getAsCXXRecordDecl();
      if (base == nullptr || !isInterface(*base)) return false;
    }
    return true;
  }

  // Portreferens: I& eller const I& där I är ett gränssnitt.
  bool isPortReference(QualType t) const {
    const auto* ref = t->getAs<LValueReferenceType>();
    if (ref == nullptr) return false;
    const CXXRecordDecl* rd = ref->getPointeeType()->getAsCXXRecordDecl();
    return rd != nullptr && isInterface(*rd);
  }

  // Objekttyp: får vara *this och stå bakom en referens, men inte kopieras
  // (en polymorf klass skulle skäras av). En värdetyp är också en objekttyp.
  // Utöver värdetyperna: polymorfa klasser och klasser med baser, där
  // baserna är objekttyper och fälten värdetyper eller portreferenser.
  std::optional<std::string> objectReason(const CXXRecordDecl& decl) {
    auto valueReason = recordReason(decl);
    if (!valueReason) return std::nullopt;
    // STN-88: inte heller en objekttyp bakom en referens.
    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(&decl);
        spec != nullptr && sharedOwnership(spec->getSpecializedTemplate()->getQualifiedNameAsString()))
      return valueReason;
    std::string name = recordName(decl);
    const CXXRecordDecl* rd = decl.getDefinition();
    if (rd == nullptr) return name + " is incomplete";
    if (rd->isLambda()) return "lambda closure type";
    if (rd->isUnion()) return name + " is a union";
    if (rd->getNumVBases() != 0) return name + " has virtual base classes";
    // Samma bas med fält två gånger (D : L, R med L : B och R : B): två
    // B-delobjekt som lånemodellens vägar inte skiljer åt (d.L::v och d.R::v
    // blir båda d.v).
    if (auto twice = repeatedBase(*rd)) return name + " has base " + *twice + " more than once";
    for (const Subobject& o : subobjects(*rd)) {
      if (o.base != nullptr) {
        const CXXRecordDecl* base = o.type()->getAsCXXRecordDecl();
        if (base == nullptr) return name + " has a dependent base class";
        if (auto r = objectReason(*base)) return name + " has base " + recordName(*base) + " (" + *r + ")";
        continue;
      }
      const FieldDecl* f = o.field;
      std::string field = "field '" + f->getNameAsString() + "' of " + name;
      if (f->isMutable()) return field + " is mutable";
      if (isPortReference(f->getType())) continue;
      if (isContextReference(f->getType())) continue;
      if (auto w = whyNotFieldType(f->getType())) return field + " has type " + typeName(f->getType()) + *w;
    }
    // Två referensfält som kan peka på samma sak: skrivningar genom det ena
    // ändrar det andra, och det går inte att se i klassen.
    std::vector<HiddenRef> refs = hiddenRefs(ctx_.getCanonicalTagType(rd));
    for (std::size_t i = 0; i < refs.size(); ++i)
      for (std::size_t j = i + 1; j < refs.size(); ++j)
        if (reaches(refs[i].type, refs[j].type) || reaches(refs[j].type, refs[i].type))
          return name + " has reference fields '" + refs[i].field->getNameAsString() + "' and '" +
                 refs[j].field->getNameAsString() + "' that may refer to the same object";
    if (auto what = userProvidedSpecialMember(*rd)) return name + " has a user-provided " + *what;
    return std::nullopt;
  }

  // Når ett objekt av typen en port? Ett gränssnitt, eller en objekttyp med
  // en portreferens (i sig eller i en bas). Värdetyper gör det aldrig.
  // `asObject`: objektet i ett anrop som inte dispatchar; då körs klassens
  // egen kropp och bara portfälten räknas. Ett final-gränssnitt är ingen port
  // (alla anrop på det avgörs statiskt).
  bool reachesPort(QualType t, bool asObject = false) {
    t = t.getNonReferenceType();
    if (t->isPointerType()) t = t->getPointeeType();  // this
    const CXXRecordDecl* rd = t.getCanonicalType()->getAsCXXRecordDecl();
    if (rd == nullptr || (rd = rd->getDefinition()) == nullptr) return false;
    if (!asObject && isInterface(*rd) && !rd->hasAttr<FinalAttr>()) return true;
    for (const CXXBaseSpecifier& b : rd->bases())
      if (reachesPort(b.getType(), true)) return true;
    for (const FieldDecl* f : rd->fields())
      if (isPortReference(f->getType())) return true;
    return false;
  }

  // Kanoniskt typnamn, samma i alla TU:er.
  static std::string typeKey(QualType t) {
    return t.getNonReferenceType().getCanonicalType().getUnqualifiedType().getAsString();
  }

  // Typerna ett objekt av typen kan innehålla (som reaches, men som mängd).
  void reachSet(QualType a, std::set<std::string>& out) {
    a = a.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    if (!out.insert(typeKey(a)).second) return;
    const CXXRecordDecl* rd = a->getAsCXXRecordDecl();
    if (rd == nullptr || (rd = rd->getDefinition()) == nullptr) return;
    for (const CXXBaseSpecifier& b : rd->bases()) reachSet(b.getType(), out);
    for (const FieldDecl* f : rd->fields())
      if (!f->getType()->isReferenceType()) reachSet(f->getType(), out);
    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(rd))
      for (const TemplateArgument& arg : spec->getTemplateArgs().asArray())
        if (arg.getKind() == TemplateArgument::Type) reachSet(arg.getAsType(), out);
  }

  // Kontextreferens: ett fält T& eller const T& där T är en värdetyp
  // (`Context& game_`). Ett dolt referensargument till varje metod: anroparen
  // ser det inte, så aliasregeln (hiddenAlias) gäller vid varje anrop.
  bool isContextReference(QualType t) {
    const auto* ref = t->getAs<LValueReferenceType>();
    return ref != nullptr && !ref->getPointeeType().isVolatileQualified() && isValueType(ref->getPointeeType());
  }

  struct HiddenRef {
    const FieldDecl* field;
    QualType type;  // T, kanoniskt utan const
    bool isConst;
  };

  // Kontextreferenserna i en objekttyp, även i baserna.
  std::vector<HiddenRef> hiddenRefs(QualType t) {
    std::vector<HiddenRef> out;
    t = t.getNonReferenceType();
    if (t->isPointerType()) t = t->getPointeeType();  // this
    const CXXRecordDecl* rd = t.getCanonicalType()->getAsCXXRecordDecl();
    if (rd == nullptr || (rd = rd->getDefinition()) == nullptr) return out;
    for (const CXXBaseSpecifier& b : rd->bases())
      for (const HiddenRef& h : hiddenRefs(b.getType())) out.push_back(h);
    for (const FieldDecl* f : rd->fields())
      if (isContextReference(f->getType())) {
        QualType p = f->getType()->getPointeeType();
        out.push_back({f, p.getCanonicalType().getUnqualifiedType(), p.isConstQualified()});
      }
    return out;
  }

  // Kan ett objekt av typen `a` innehålla ett objekt av typen `target`
  // (samma typ, ett fält, en bas eller ett typargument, transitivt)?
  bool reaches(QualType a, QualType target) {
    std::set<const Type*> seen;  // rekursiva typer (STN-63)
    return reaches(a, target, seen);
  }
  bool reaches(QualType a, QualType target, std::set<const Type*>& seen) {
    a = a.getNonReferenceType().getCanonicalType().getUnqualifiedType();
    target = target.getCanonicalType().getUnqualifiedType();
    if (a == target) return true;
    if (!seen.insert(a.getTypePtr()).second) return false;
    const CXXRecordDecl* rd = a->getAsCXXRecordDecl();
    if (rd == nullptr || (rd = rd->getDefinition()) == nullptr) return false;
    for (const CXXBaseSpecifier& b : rd->bases())
      if (reaches(b.getType(), target, seen)) return true;
    for (const FieldDecl* f : rd->fields())
      if (!f->getType()->isReferenceType() && reaches(f->getType(), target, seen)) return true;
    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(rd))
      for (const TemplateArgument& arg : spec->getTemplateArgs().asArray())
        if (arg.getKind() == TemplateArgument::Type && reaches(arg.getAsType(), target, seen)) return true;
    return false;
  }

  // Typen bakom en referens eller *this: värde- eller objekttyp.
  std::optional<std::string> whyNotObject(QualType t) {
    if (const CXXRecordDecl* rd = t.getCanonicalType()->getAsCXXRecordDecl();
        rd != nullptr && !t.getCanonicalType().isVolatileQualified()) {
      if (auto r = objectReason(*rd)) return " (" + *r + ")";
      return std::nullopt;
    }
    return whyNotValue(t);
  }

  // En struct är en värdetyp om en kopia är en kopia av värden och inget i
  // den kan peka ut annat minne:
  //  - betrodd klasstemplate (`type std::optional` i trust-filen) där alla
  //    typargument är värdetyper, eller
  //  - ingen union/lambda, inga basklasser eller virtuella funktioner, inga
  //    mutable-fält, alla fält värdetyper, och antingen trivialt kopierbar
  //    eller utan egna kopierings-/flytt-/destruktorfunktioner (då är en
  //    kopia en medlemsvis kopia av värden).
  //  - rekursiv via betrodda typer (STN-63, `struct Node { std::vector<Node>
  //    kids; }`): en värdetyp om den är det under antagandet att den är det
  //    (koinduktivt). Utan skyddet går whyNotValue/recordReason runt i oändlighet.
  std::optional<std::string> recordReason(const CXXRecordDecl& decl) {
    std::optional<std::string> why = structuralRecordReason(decl);
    if (!why) return why;
    // En obetrodd std-mall: dess interna fält (`_M_t`) säger inget för
    // användaren. Domen är densamma, bara skälet pekar på trust-filen.
    if (std::optional<std::string> tmpl = untrustedStdTemplate(decl))
      return recordName(decl) + " is not a trusted type (add 'type " + *tmpl + "' to a trust file)";
    return why;
  }

  // Mallnamnet för en specialisering av en publik std-mall i en systemheader
  // som inte står i trust-filen (och inte har en egen regel: STN-71, STN-88).
  std::optional<std::string> untrustedStdTemplate(const CXXRecordDecl& decl) const {
    const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(&decl);
    if (spec == nullptr) return std::nullopt;
    if (!decl.getASTContext().getSourceManager().isInSystemHeader(decl.getLocation())) return std::nullopt;
    std::string tmpl = spec->getSpecializedTemplate()->getQualifiedNameAsString();
    if (!tmpl.starts_with("std::") || tmpl.find("::_") != std::string::npos) return std::nullopt;
    if (trust_.types.contains(tmpl) || tmpl == "std::initializer_list" || sharedOwnership(tmpl)) return std::nullopt;
    return tmpl;
  }

  std::optional<std::string> structuralRecordReason(const CXXRecordDecl& decl) {
    std::string name = recordName(decl);
    const auto* key = decl.getCanonicalDecl();
    if (!inProgress_.insert(key).second) return std::nullopt;  // antagandet
    struct Leave {
      std::set<const CXXRecordDecl*>& s;
      const CXXRecordDecl* k;
      ~Leave() { s.erase(k); }
    } leave{inProgress_, key};
    if (const auto* spec = dyn_cast<ClassTemplateSpecializationDecl>(&decl)) {
      std::string tmpl = spec->getSpecializedTemplate()->getQualifiedNameAsString();
      // STN-71: en vy av en temporär array, aldrig en värdetyp (inte heller
      // med `type std::initializer_list` i trust-filen).
      if (tmpl == "std::initializer_list") return name + " is a view of a temporary array";
      // STN-88: delat ägande, aldrig en värdetyp (inte heller med `type
      // std::shared_ptr` i trust-filen): två olika vägar kan vara samma objekt.
      if (sharedOwnership(tmpl)) return name + " shares ownership (two values may be the same object)";
      if (trust_.types.contains(tmpl)) {
        for (const TemplateArgument& a : spec->getTemplateArgs().asArray())
          if (auto r = templateArgReason(a, name)) return r;
        assumed_.insert("type " + tmpl);
        return std::nullopt;
      }
    }
    const CXXRecordDecl* rd = decl.getDefinition();
    if (rd == nullptr) return name + " is incomplete";
    if (rd->isLambda()) return "lambda closure type";
    if (rd->isUnion()) return name + " is a union";
    if (rd->isPolymorphic()) return name + " is polymorphic";
    // STN-2: icke-virtuella baser som själva är värdetyper (deras fält är en
    // del av objektet). Virtuella baser delar en underobjekt mellan vägar.
    if (rd->getNumVBases() != 0) return name + " has virtual base classes";
    // Samma bas med fält två gånger (D : L, R med L : B och R : B): två
    // B-delobjekt som lånemodellens vägar inte skiljer åt (d.L::v och d.R::v
    // blir båda d.v).
    if (auto twice = repeatedBase(*rd)) return name + " has base " + *twice + " more than once";
    for (const Subobject& o : subobjects(*rd)) {
      if (o.base != nullptr) {
        if (o.base->isVirtual()) return name + " has virtual base classes";
        if (auto w = whyNotValue(o.type())) return "base " + typeName(o.type()) + " of " + name + *w;
        continue;
      }
      std::string field = "field '" + o.field->getNameAsString() + "' of " + name;
      if (o.field->isMutable()) return field + " is mutable";
      if (auto w = whyNotFieldType(o.type())) return field + " has type " + typeName(o.type()) + *w;
    }
    // STN-72: egna kopierings-/flyttfunktioner gör inte typen till en
    // icke-värdetyp (fälten är värden, så en kopia kan inte dela minne). De
    // är funktioner som granskas, och varje implicit anrop av dem är ett
    // anrop (STN-23, STN-67).
    return std::nullopt;
  }

  // Parametrar: värdetyp, const T& (läsparameter) eller T& (in out) där T är
  // en värdetyp. Aliasing mellan referensparametrar stoppas hos anroparen.
  Reason paramReason(const ParmVarDecl& p) {
    QualType t = p.getType();
    std::string head = "parameter '" + p.getNameAsString() + "' has type " + typeName(t);
    // std::span<const T> som värde: en läsvy, som const std::vector<T>& (Span.hpp).
    if (const auto* spec = constSpanSpec(t); spec != nullptr && !t->isReferenceType()) {
      if (auto w = whyNotValue(spanElement(*spec))) return head + *w;
      return std::nullopt;
    }
    // STN-71: std::initializer_list<T> som värde: en läsvy av anroparens
    // temporära array, som lever hela anropet.
    if (auto element = initializerListElement(t); element && !t->isReferenceType()) {
      if (auto w = whyNotListElement(*element)) return head + *w;
      return std::nullopt;
    }
    if (const auto* ref = t->getAs<LValueReferenceType>()) {
      // STN-75: `const T (&a)[N]`, `T (&a)[N]`: en ägd array genom referens.
      if (ref->getPointeeType()->isConstantArrayType()) {
        if (auto w = whyNotOwnedArray(ref->getPointeeType())) return head + *w;
        return std::nullopt;
      }
      if (auto w = whyNotObject(ref->getPointeeType())) return head + *w;
      return std::nullopt;
    }
    // STN-72: T&& i flyttkonstruktorn och flyttilldelningen av T själv, som
    // en T& (anroparen har gett upp värdet: std::move(x), STN-57, eller en
    // temporär).
    if (isMoveSource(p)) {
      if (auto w = whyNotValue(t.getNonReferenceType())) return head + *w;
      return std::nullopt;
    }
    if (auto w = whyNotValue(t)) return head + *w;
    return std::nullopt;
  }

  // Parametern T&& i T(T&&) eller T::operator=(T&&).
  static bool isMoveSource(const ParmVarDecl& p) {
    if (!p.getType()->isRValueReferenceType()) return false;
    const auto* md = dyn_cast_or_null<CXXMethodDecl>(p.getDeclContext());
    if (md == nullptr) return false;
    const auto* ctor = dyn_cast<CXXConstructorDecl>(md);
    return (ctor != nullptr && ctor->isMoveConstructor()) || md->isMoveAssignmentOperator();
  }


  // Medlemsfunktioner: `this` är en const T& (const-metod) eller T& (övriga).
  Reason methodReason(const CXXMethodDecl& md) {
    if (md.isStatic()) return std::nullopt;  // ingen this: som en fri funktion
    if (md.isExplicitObjectMemberFunction()) return "explicit object member function (not supported yet)";
    if (md.isVolatile()) return "volatile member function";
    if (auto r = objectReason(*md.getParent()))
      return "member of non-object type " + recordName(*md.getParent()) + " (" + *r + ")";
    return std::nullopt;
  }

 private:
  std::optional<std::string> templateArgReason(const TemplateArgument& a, const std::string& owner) {
    if (a.getKind() == TemplateArgument::Pack) {
      for (const TemplateArgument& p : a.pack_elements())
        if (auto r = templateArgReason(p, owner)) return r;
      return std::nullopt;
    }
    if (a.getKind() != TemplateArgument::Type) return std::nullopt;  // heltal o.d.
    if (auto w = whyNotValue(a.getAsType()))
      return "template argument " + typeName(a.getAsType()) + " of " + owner +
             " is not a value type" + *w;
    return std::nullopt;
  }

  static std::optional<std::string> userProvidedSpecialMember(const CXXRecordDecl& rd) {
    for (const CXXConstructorDecl* c : rd.ctors()) {
      if (c->isCopyConstructor() && c->isUserProvided()) return "copy constructor";
      if (c->isMoveConstructor() && c->isUserProvided()) return "move constructor";
    }
    for (const CXXMethodDecl* m : rd.methods()) {
      if (m->isCopyAssignmentOperator() && m->isUserProvided()) return "copy assignment operator";
      if (m->isMoveAssignmentOperator() && m->isUserProvided()) return "move assignment operator";
    }
    return std::nullopt;  // en egen destruktor är tillåten (STN-68)
  }

 public:
  const ASTContext& context() const { return ctx_; }

 private:
  const ASTContext& ctx_;
  PrintingPolicy policy_;
  const TrustList& trust_;
  std::set<std::string>& assumed_;
  const LoanSummaries* summaries_;
  std::set<const CXXRecordDecl*> inProgress_;  // recordReason på väg ner (STN-63)
};

// ---------------------------------------------------------------------------
// Vägar (variabel/fält) för aliasing-kontroll
// ---------------------------------------------------------------------------

// a, a.b.c, this->x (eller bara x i en metod), *this. Två vägar överlappar
// om de har samma rot och den ena är ett prefix av den andra.
//
// En lokal referens bunden till en väg löses upp till den vägen. En referens
// bunden till ett element (`Vec2& p = t.players[i]`) blir en *öppen* väg till
// behållaren (`t.players`): någon okänd del av den. Fältåtkomst på en öppen
// väg förfinar den inte. Vägarna används för G4 (vad som skrivs, `written_`),
// std::move (STN-57) och dolda referenser (hiddenAlias); lån och alias mellan
// argument avgör lånemodellen (LoanCheck).
struct Path {
  const Decl* root = nullptr;  // nullptr = this
  std::vector<const FieldDecl*> fields;
  std::string text;            // som i källan: "b.pos", "p"
  bool open = false;

  bool overlaps(const Path& o) const {
    if (root != o.root) return false;
    for (std::size_t i = 0; i < std::min(fields.size(), o.fields.size()); ++i)
      if (fields[i] != o.fields[i]) return false;
    return true;
  }
};

// STN-73: en icke-const metod i C som returnerar C& (operator=, +=). I
// Stone får den bara returnera *this, så anroparen vet att resultatet är
// objektet. Inte för betrodd kod och std, vars kropp inte granskas här.
bool returnsThis(const FunctionDecl& fd) {
  const auto* md = dyn_cast<CXXMethodDecl>(&fd);
  if (md == nullptr || !md->isInstance() || md->isConst() || isa<CXXConstructorDecl>(md)) return false;
  const auto* ref = md->getReturnType()->getAs<LValueReferenceType>();
  if (ref == nullptr || ref->getPointeeType().isConstQualified() || ref->getPointeeType().isVolatileQualified())
    return false;
  const ASTContext& ctx = md->getASTContext();
  return ctx.hasSameType(ref->getPointeeType().getCanonicalType(),
                         ctx.getCanonicalTagType(md->getParent()));
}

// STN-76: en icke-const metod som returnerar T& (T inte klassen själv,
// int& operator[]). Den får bara returnera en väg in i *this.
bool returnsMemberRef(const FunctionDecl& fd) {
  const auto* md = dyn_cast<CXXMethodDecl>(&fd);
  if (md == nullptr || !md->isInstance() || md->isConst() || isa<CXXConstructorDecl>(md) || returnsThis(fd))
    return false;
  if (md->getRefQualifier() == RQ_RValue) return false;
  const auto* ref = md->getReturnType()->getAs<LValueReferenceType>();
  return ref != nullptr && !ref->getPointeeType().isConstQualified() && !ref->getPointeeType().isVolatileQualified();
}

bool returnsMemberRefChecked(const FunctionDecl& fd) {
  return returnsMemberRef(fd) && !fd.getASTContext().getSourceManager().isInSystemHeader(fd.getLocation()) &&
         !hasAnnotation(fd, "pork::trusted");
}

// STN-10: en egen const-metod som returnerar const T& och inte har några
// referensparametrar: resultatet kan bara peka in i *this.
bool returnsConstRefIntoThis(const FunctionDecl& fd) {
  const auto* md = dyn_cast<CXXMethodDecl>(&fd);
  if (md == nullptr || !md->isInstance() || !md->isConst()) return false;
  const auto* ref = md->getReturnType()->getAs<LValueReferenceType>();
  if (ref == nullptr || !ref->getPointeeType().isConstQualified()) return false;
  for (const ParmVarDecl* p : md->parameters())
    if (p->getType()->isReferenceType() || p->getType()->isPointerType()) return false;
  return !fd.getASTContext().getSourceManager().isInSystemHeader(fd.getLocation()) &&
         !hasAnnotation(fd, "pork::trusted");
}

bool returnsThisChecked(const FunctionDecl& fd) {
  return returnsThis(fd) && !fd.getASTContext().getSourceManager().isInSystemHeader(fd.getLocation()) &&
         !hasAnnotation(fd, "pork::trusted");
}

std::vector<Subobject> subobjects(const CXXRecordDecl& decl) {
  std::vector<Subobject> out;
  const CXXRecordDecl* rd = decl.getDefinition();
  if (rd == nullptr) return out;
  for (const CXXBaseSpecifier& b : rd->bases()) out.push_back({&b, nullptr});
  for (const FieldDecl* f : rd->fields()) out.push_back({nullptr, f});
  return out;
}

// Har typen (eller någon av dess baser) fält?
bool hasFields(QualType t) {
  const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
  if (rd == nullptr || !rd->hasDefinition()) return true;
  for (const Subobject& o : subobjects(*rd))
    if (o.field != nullptr || hasFields(o.type())) return true;
  return false;
}

// En bas med fält som förekommer mer än en gång bland delobjekten.
std::optional<std::string> repeatedBase(const CXXRecordDecl& rd) {
  std::map<const Type*, int> seen;
  std::optional<std::string> found;
  auto walk = [&](auto& self, const CXXRecordDecl& r) -> void {
    for (const CXXBaseSpecifier& b : r.bases()) {
      const CXXRecordDecl* base = b.getType()->getAsCXXRecordDecl();
      if (base == nullptr || !base->hasDefinition()) continue;
      const Type* key = b.getType()->getCanonicalTypeUnqualified().getTypePtr();
      if (++seen[key] == 2 && !found && hasFields(b.getType())) found = base->getQualifiedNameAsString();
      self(self, *base->getDefinition());
    }
  };
  walk(walk, rd);
  return found;
}

const Expr* stripNoOp(const Expr* e) {
  while (true) {
    e = e->IgnoreParens();
    const auto* ice = dyn_cast<ImplicitCastExpr>(e);
    if (ice == nullptr || ice->getCastKind() != CK_NoOp) return e;
    e = ice->getSubExpr();
  }
}

// En temporär, även i ett standardargument (`comp = {}` i std::ranges::sort):
// det utvärderas på nytt vid varje anrop.
bool isTemporary(const Expr* e) {
  e = stripNoOp(e);
  if (const auto* d = dyn_cast<CXXDefaultArgExpr>(e)) e = stripNoOp(d->getExpr());
  return isa<MaterializeTemporaryExpr>(e);
}

// ---------------------------------------------------------------------------
// Satser och uttryck
// ---------------------------------------------------------------------------

class Checker {
 public:
  Checker(TypeRules& types, std::map<std::string, Callee>& callees)
      : types_(types), callees_(callees) {}

  // Skalärer i `fd` som får deklareras utan initierare: clangs analys av
  // oinitierade värden har körts, och `maybe` är de som kan läsas otilldelade.
  void allowUnassigned(const FunctionDecl& fd, std::set<const VarDecl*> maybe) {
    analysed_ = &fd;
    maybeUnassigned_ = std::move(maybe);
  }
  // Funktionen returnerar `const T&` (se checkFunction och refReturnSource).
  void allowRefReturn() { refReturn_ = true; }
  // Funktionen returnerar `const T*` (se checkFunction och pointerReturnSource).
  void allowPointerReturn() { pointerReturn_ = true; }
  void allowThisReturn() { thisReturn_ = true; }
  void allowMemberRef() { memberRef_ = true; }

  // Levande variabler efter varje uttryck i funktionens CFG (STN-57, std::move).
  void setLiveness(const FunctionDecl& fd, std::map<const Stmt*, std::set<const VarDecl*>> live) {
    liveAfter_ = std::move(live);
    moveFunction_ = &fd;
  }

  Checker(TypeRules& types, std::map<std::string, Callee>& callees, std::set<const VarDecl*>& written)
      : types_(types), callees_(callees), written_(written) {}

  // Funktionskroppen: inledande PORK_PRE/PORK_POST är rena villkor, sedan
  // vanliga satser. `fd` = funktionen (nullptr för en lambdakropp: där finns
  // inga kontrakt).
  // STN-66: initieringslistan i en konstruktor, före kroppen. Objektet är inte
  // färdigt, så ingen initierare får använda *this; när kroppen börjar ska
  // alla fält vara initierade.
  Reason ctorInits(const CXXConstructorDecl& ctor) {
    const CXXRecordDecl& rd = *ctor.getParent();
    std::set<const FieldDecl*> done;
    std::set<const Type*> bases;
    for (const CXXCtorInitializer* init : ctor.inits()) {
      const Expr* e = init->getInit();
      if (init->isBaseInitializer() && isa<InitListExpr>(e->IgnoreImplicit())) {
        // Aggregatbas: `Coordinates{a, b}` (STN-2), granskas som en egen initiering.
        if (usesThis(e)) return "initializer of base " + types_.typeName(QualType(init->getBaseClass(), 0)) +
                                " uses *this";
        if (auto r = effectAllowed(e)) return r;
        bases.insert(init->getBaseClass()->getCanonicalTypeUnqualified().getTypePtr());
        continue;
      }
      if (init->isBaseInitializer() || init->isDelegatingInitializer()) {
        const auto* ce = dyn_cast<CXXConstructExpr>(e->IgnoreImplicit());
        if (ce == nullptr) return std::string("unsupported base initializer");
        if (init->isBaseInitializer()) {
          // En trivial standardkonstruktor lämnar basens fält oinitierade
          // (som ett fält, STN-66); en tom bas har inget att initiera.
          if (isUninitializedConstruct(ce) && hasFields(QualType(init->getBaseClass(), 0)))
            return "base " + types_.typeName(QualType(init->getBaseClass(), 0)) + " of " + recordName(rd) +
                   " is not initialized by the constructor";
          bases.insert(init->getBaseClass()->getCanonicalTypeUnqualified().getTypePtr());
        }
        if (usesThis(ce)) return "initializer of base " + types_.typeName(QualType(init->getBaseClass(), 0)) +
                                 " uses *this";
        if (auto r = constructorCall(*ce)) return r;
        if (init->isDelegatingInitializer()) return std::nullopt;  // målet initierar alla fält
        continue;
      }
      const FieldDecl* f = init->getMember();
      if (f == nullptr || init->isIndirectMemberInitializer())
        return std::string("member initializer of an anonymous struct or union");
      std::string name = "'" + f->getNameAsString() + "'";
      // En standardinitierare som konstruktorn använder granskas här, i
      // konstruktorns sammanhang, inte som CXXDefaultInitExpr i expr().
      if (const auto* d = dyn_cast<CXXDefaultInitExpr>(e)) e = d->getExpr();
      if (usesThisExceptInitialized(e, rd, done)) return "initializer of " + name + " uses *this";
      if (f->getType()->isReferenceType()) {
        const auto* ref = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
        const auto* p = ref == nullptr ? nullptr : dyn_cast<ParmVarDecl>(ref->getDecl());
        if (p == nullptr || !p->getType()->isReferenceType())
          return "reference field " + name + " must be bound to a reference parameter";
        markRead(e);
      } else if (types_.isOwnedArray(f->getType())) {
        if (auto r = arrayElements(e)) return r;
      } else if (auto r = effectAllowed(e)) {  // en egen initiering, som `T x = f(y);`
        return r;
      }
      done.insert(f);
    }
    // En bas utan initierare (trivial standardkonstruktor) lämnar sina fält
    // oinitierade, som ett fält utan initierare.
    for (const Subobject& o : subobjects(rd)) {
      if (o.field != nullptr && !done.contains(o.field))
        return "field '" + o.field->getNameAsString() + "' of " + recordName(rd) + " is not initialized by the constructor";
      if (o.base != nullptr && !bases.contains(o.type()->getCanonicalTypeUnqualified().getTypePtr()) &&
          hasFields(o.type()))
        return "base " + types_.typeName(o.type()) + " of " + recordName(rd) + " is not initialized by the constructor";
    }
    return std::nullopt;
  }

  Reason body(const Stmt* s, const FunctionDecl* fd = nullptr) {
    const auto* cs = dyn_cast_or_null<CompoundStmt>(s);
    if (cs == nullptr) return stmt(s);
    auto it = cs->body_begin();
    for (; it != cs->body_end(); ++it) {
      if (const Expr* cond = preCondition(*it)) {
        if (fd == nullptr) return std::string("PORK_PRE in a lambda");
        if (auto r = expr(cond)) return r;
      } else if (auto a = fd == nullptr ? std::nullopt
                                        : assertMacro(*it, types_.context().getSourceManager(),
                                                      types_.context().getLangOpts())) {
        if (auto r = expr(a->condition)) return r;  // förvillkor (se CTranslate)
      } else if (fd != nullptr && disabledAssert(*it, types_.context().getSourceManager(),
                                                 types_.context().getLangOpts())) {
        // assert med NDEBUG: samma plats i inledningen, inget att granska.
      } else if (const LambdaExpr* post = postCondition(*it)) {
        if (fd == nullptr) return std::string("PORK_POST in a lambda");
        if (auto r = postReason(*fd, *post)) return r;
      } else if (flowRequirement(*it) != nullptr) {
        // BRZ-1: platserna granskas mot flödet (FlowBuild), inget körs.
        if (fd == nullptr) return std::string("PORK_DEPENDS in a lambda");
      } else {
        break;
      }
    }
    for (; it != cs->body_end(); ++it)
      if (auto r = stmt(*it)) return r;
    return std::nullopt;
  }

  Reason stmt(const Stmt* s) { return stmtImpl(withoutHints(s)); }

 private:
  Reason stmtImpl(const Stmt* s) {
    if (s == nullptr) return std::nullopt;

    // assert(c): bara villkoret är koden (se Assert.hpp), och det är rent.
    if (auto a = assertMacro(s, types_.context().getSourceManager(), types_.context().getLangOpts()))
      return expr(a->condition);

    if (const auto* e = dyn_cast<Expr>(s)) {
      const Expr* inner = e;
      if (const auto* fe = dyn_cast<FullExpr>(inner)) inner = fe->getSubExpr();
      if (const auto* te = dyn_cast<CXXThrowExpr>(inner->IgnoreParens())) return throwStmt(*te);
      return exprStmt(e);
    }

    if (const auto* cs = dyn_cast<CompoundStmt>(s)) {
      for (const Stmt* child : cs->body())
        if (auto r = stmt(child)) return r;
      return std::nullopt;
    }
    if (isa<NullStmt, BreakStmt, ContinueStmt>(s)) return std::nullopt;
    if (const auto* rs = dyn_cast<ReturnStmt>(s)) {
      if (thisReturn_) {  // STN-73
        const auto* u = rs->getRetValue() ? dyn_cast<UnaryOperator>(stripNoOp(rs->getRetValue())) : nullptr;
        if (u == nullptr || u->getOpcode() != UO_Deref || !isa<CXXThisExpr>(stripNoOp(u->getSubExpr())))
          return std::string("a method returning its own class by reference may only return *this");
        return std::nullopt;
      }
      if (memberRef_) {  // STN-76
        if (auto r = memberRefSource(rs->getRetValue())) return r;
        return effectAllowedInBranches(rs->getRetValue());
      }
      if (pointerReturn_) return pointerReturnSource(rs->getRetValue());
      if (refReturn_) {
        if (auto r = refReturnSource(rs->getRetValue())) return r;
        markRead(rs->getRetValue());  // const T& till behållaren: en const-läsning
        return expr(rs->getRetValue());
      }
      return effectAllowed(rs->getRetValue());
    }
    if (const auto* ds = dyn_cast<DeclStmt>(s)) return declStmt(ds);
    // I källordning: villkorsvariabler och init-satser deklarerar lån och
    // get_if-pekare som villkor och kropp använder.
    if (const auto* is = dyn_cast<IfStmt>(s)) {
      if (is->isConsteval()) return "unsupported statement: if consteval";
      return inOrder([&] { return decls(is->getInit()); },
                     [&] { return decls(is->getConditionVariableDeclStmt()); },
                     // Hela villkoret får vara ett anrop med sidoeffekt: det
                     // utvärderas en gång, före grenarna, som en sats.
                     [&] { return effectAllowed(is->getCond()); }, [&] { return stmt(is->getThen()); },
                     [&] { return stmt(is->getElse()); });
    }
    if (const auto* ws = dyn_cast<WhileStmt>(s))
      return inOrder([&] { return decls(ws->getConditionVariableDeclStmt()); },
                     [&] { return expr(ws->getCond()); }, [&] { return stmt(ws->getBody()); });
    if (const auto* d = dyn_cast<DoStmt>(s))
      return inOrder([&] { return stmt(d->getBody()); }, [&] { return expr(d->getCond()); });
    if (const auto* fs = dyn_cast<ForStmt>(s))
      return inOrder([&] { return decls(fs->getInit()); },
                     [&] { return decls(fs->getConditionVariableDeclStmt()); },
                     [&] { return expr(fs->getCond()); }, [&] { return exprStmt(fs->getInc()); },
                     [&] { return stmt(fs->getBody()); });
    if (const auto* ss = dyn_cast<SwitchStmt>(s))
      return inOrder([&] { return decls(ss->getInit()); },
                     [&] { return decls(ss->getConditionVariableDeclStmt()); },
                     [&] { return expr(ss->getCond()); }, [&] { return stmt(ss->getBody()); });
    if (const auto* c = dyn_cast<CaseStmt>(s))
      return inOrder([&] { return expr(c->getLHS()); }, [&] { return expr(c->getRHS()); },
                     [&] { return stmt(c->getSubStmt()); });
    if (const auto* d = dyn_cast<DefaultStmt>(s)) return stmt(d->getSubStmt());
    if (const auto* rf = dyn_cast<CXXForRangeStmt>(s)) return rangeFor(*rf);

    return std::string("unsupported statement: ") + s->getStmtClassName();
  }

  // STN-83: `throw std::length_error("...");` som egen sats: funktionen
  // avslutas utan att returnera. Bara undantagstyperna i <stdexcept> med en
  // strängliteral; konstruktorn registreras som antagandet "throws <typ>".
  // `throw` i ett uttryck, omkastning och egna typer avvisas (STN-13).
  Reason throwStmt(const CXXThrowExpr& te) {
    static const std::set<std::string> allowed = {
        "std::logic_error",  "std::domain_error", "std::invalid_argument", "std::length_error",
        "std::out_of_range", "std::runtime_error", "std::range_error",     "std::overflow_error",
        "std::underflow_error"};
    const Expr* op = te.getSubExpr();
    if (op == nullptr) return std::string("rethrow (throw;)");
    for (;;) {
      op = op->IgnoreParens();
      if (const auto* x = dyn_cast<MaterializeTemporaryExpr>(op)) op = x->getSubExpr();
      else if (const auto* x = dyn_cast<CXXBindTemporaryExpr>(op)) op = x->getSubExpr();
      else if (const auto* x = dyn_cast<CXXFunctionalCastExpr>(op)) op = x->getSubExpr();
      else if (const auto* x = dyn_cast<ImplicitCastExpr>(op)) op = x->getSubExpr();
      else break;
    }
    const auto* ce = dyn_cast<CXXConstructExpr>(op);
    const CXXRecordDecl* rd = ce != nullptr ? ce->getConstructor()->getParent() : nullptr;
    std::string type = rd != nullptr ? rd->getQualifiedNameAsString() : "";
    if (!allowed.contains(type))
      return std::string("throws something other than a <stdexcept> type with a string literal");
    if (ce->getNumArgs() != 1 || !isa<StringLiteral>(ce->getArg(0)->IgnoreParenImpCasts()))
      return "the argument of " + type + " must be a string literal";
    std::string usr = usrOf(*ce->getConstructor());
    if (usr.empty()) return std::string("throws (no USR)");
    callees_.emplace(usr, Callee{type + "::" + rd->getNameAsString(), "throws " + type, true, ""});
    if (const CXXDestructorDecl* d = rd->getDestructor())
      if (std::string du = usrOf(*d); !du.empty()) callees_.emplace(du, Callee{displayName(*d), "throws " + type, true, ""});
    return std::nullopt;
  }

  // for (T& x : v): v är en std::vector som är en väg. Behållaren är lånad
  // under hela loopen (iteratorerna får inte ogiltigförklaras); x är ett
  // element (öppen väg via x) eller en kopia.
  // En variabel med statisk lagring som staticVariable godtar (constexpr
  // eller const med konstant initiering).
  static bool constantTable(const Expr* e) {
    const auto* dr = dyn_cast<DeclRefExpr>(e->IgnoreParens());
    const auto* vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
    return vd != nullptr && !vd->hasLocalStorage() && !staticVariable(*vd);
  }

  // Ett anrop (inte en operator på en vy) vars resultat är en const T&.
  static bool returnsConstRef(const Expr* e) {
    const auto* call = dyn_cast<CallExpr>(e->IgnoreImplicit()->IgnoreParens());
    const FunctionDecl* f = call ? call->getDirectCallee() : nullptr;
    if (f == nullptr || !call->isLValue()) return false;
    const auto* ref = f->getReturnType()->getAs<LValueReferenceType>();
    return ref != nullptr && ref->getPointeeType().isConstQualified();
  }

  Reason rangeFor(const CXXForRangeStmt& rf) {
    if (rf.getInit() != nullptr) return "range-for with an init statement";
    if (auto r = destroyed(rf.getLoopVariable()->getType())) return r;  // STN-68: varje varv
    const Expr* range = rf.getRangeInit();
    const CXXRecordDecl* rd = range->getType()->getAsCXXRecordDecl();
    const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd);
    // En vy (lånevärde, t.ex. std::span): lånemodellen håller det den lånar
    // levande genom loopen.
    // En betrodd std::string_view är både värdetyp (trust-filen) och vy
    // ([[gsl::Pointer]]): loopen går över det den lånar.
    bool span = types_.isLoanValue(range->getType()) &&
                (!types_.isValueType(range->getType()) || (rd != nullptr && TypeRules::isGslPointerRecord(*rd)));
    std::string tmpl = spec ? spec->getSpecializedTemplate()->getQualifiedNameAsString() : "";
    bool sequence = tmpl == "std::vector" || tmpl == "std::array" ||
                    (tmpl == "std::basic_string" && spec->getTemplateArgs()[0].getAsType()->isCharType());
    bool associative = tmpl == "std::map" || tmpl == "std::set";
    // STN-75: en ägd array som namnges direkt; loopen går över dess element
    // (inga anrop: början och slut är adresser i arrayen).
    bool array = isOwnedArrayName(range);
    if (!span && !sequence && !associative && !array)
      return "range-for over " + types_.typeName(range->getType()) +
             " (expected std::vector, std::array, std::string, std::map, std::set, a view or an array)";
    std::optional<Path> container = pathOf(range);
    // En konstant tabell (statisk lagring, const) har ingen väg, men ändras aldrig.
    bool table = array && !container && constantTable(range);
    // STN-98: ett anrop som returnerar const T& är ingen temporär. Det lånade
    // (och i C++23 temporärer i intervallet, P2718) håller lånemodellen levande.
    bool returned = !container && !table && returnsConstRef(range);
    if (!container && !table && !returned) return "range-for over a temporary (needs a variable or field)";
    if (auto r = expr(range)) return r;
    if (!span && !array)
      if (auto w = types_.whyNotValue(range->getType())) return "range-for over a non-value type" + *w;
    if (span)  // begin()/end() på spanen är betrodda anrop
      for (const DeclStmt* ds : {rf.getBeginStmt(), rf.getEndStmt()}) {
        const auto* it = ds ? dyn_cast_or_null<VarDecl>(ds->getSingleDecl()) : nullptr;
        const Expr* init = it ? it->getInit() : nullptr;
        if (const auto* mc = init ? dyn_cast<CXXMemberCallExpr>(init->IgnoreImplicit()) : nullptr)
          if (auto r = recordCallee(*mc->getMethodDecl())) return r;
      }

    const VarDecl* var = rf.getLoopVariable();
    QualType vt = var->getType();
    const auto* ref = vt->getAs<LValueReferenceType>();
    if (vt->isRValueReferenceType()) return "range-for variable '" + var->getNameAsString() + "' is T&&";
    // STN-72: en loopvariabel som kopia kopieras varje varv.
    if (auto r = copiedBy(var->getInit())) return r;
    // STN-71: över nästlade listor är elementet en inre lista, en vy som
    // lever lika länge som den yttre; bara som const&.
    bool innerList = span && ref != nullptr && ref->getPointeeType().isConstQualified() &&
                     types_.initializerListElement(ref->getPointeeType()).has_value();
    if (innerList) {
      if (auto w = types_.whyNotListElement(ref->getPointeeType()))
        return "range-for variable '" + var->getNameAsString() + "' has type " + types_.typeName(vt) + *w;
    } else if (auto w = ref != nullptr && ref->getPointeeType()->isConstantArrayType()
                            ? types_.whyNotOwnedArray(ref->getPointeeType())  // en rad i m[N][M]
                            : types_.whyNotValue(ref ? ref->getPointeeType() : vt)) {
      return "range-for variable '" + var->getNameAsString() + "' has type " + types_.typeName(vt) + *w;
    }

    if (associative && ref != nullptr && !ref->getPointeeType().isConstQualified())
      return "range-for over std::map or std::set requires a read-only element reference";

    if (associative) {
      // Bara behållarens egen elementtyp: en implicit konvertering skulle
      // kunna anropa användarkod som den syntetiska loopinitieraren döljer.
      QualType element = ref ? ref->getPointeeType() : vt;
      const auto& ctx = types_.context();
      const auto& args = spec->getTemplateArgs();
      bool exact = ctx.hasSameUnqualifiedType(element, args[0].getAsType());
      if (tmpl == "std::map") {
        const auto* pair = dyn_cast_or_null<ClassTemplateSpecializationDecl>(element->getAsCXXRecordDecl());
        exact = pair != nullptr && pair->getSpecializedTemplate()->getQualifiedNameAsString() == "std::pair" &&
                ctx.hasSameType(pair->getTemplateArgs()[0].getAsType(), args[0].getAsType().withConst()) &&
                ctx.hasSameType(pair->getTemplateArgs()[1].getAsType(), args[1].getAsType());
      }
      if (!exact) return "range-for over std::map or std::set requires the container's element type";
    }

    const auto* bindings = dyn_cast<DecompositionDecl>(var);
    if (bindings != nullptr) {
      if (!associative || tmpl != "std::map" || ref == nullptr)
        return "range-for structured bindings require const auto& over std::map";
      for (const BindingDecl* binding : bindings->bindings()) {
        if (auto w = types_.whyNotValue(binding->getType()))
          return "range-for binding '" + binding->getNameAsString() + "' has type " +
                 types_.typeName(binding->getType()) + *w;
        const VarDecl* holding = binding->getHoldingVar();
        const Expr* init = holding ? holding->getInit() : nullptr;
        const auto* get = dyn_cast_or_null<CallExpr>(init ? init->IgnoreImplicit() : nullptr);
        const FunctionDecl* callee = get ? get->getDirectCallee() : nullptr;
        if (callee == nullptr || callee->getQualifiedNameAsString() != "std::get")
          return "range-for structured binding does not use std::get";
        if (auto r = recordCallee(*callee)) return r;
      }
    }

    if (ref != nullptr && container) {
      Path element = *container;
      element.open = true;
      refs_[var] = element;
    }
    if (bindings != nullptr)
      for (const BindingDecl* binding : bindings->bindings())
        bindingPaths_.emplace(binding, refs_.at(var));
    Reason r = stmt(rf.getBody());
    if (bindings != nullptr)
      for (const BindingDecl* binding : bindings->bindings()) bindingPaths_.erase(binding);
    return r;
  }

  enum class Mode { value, constRef, mutRef };

  // Ett anrop uppdelat: funktion, ev. objekt (med läge) och argument.
  std::set<const Expr*> handedOver_;  // o.release() direkt till en ny ägare (STN-90)

  struct CallParts {
    const FunctionDecl* callee = nullptr;
    const Expr* object = nullptr;
    bool arrow = false;
    Mode objectMode = Mode::value;
    std::vector<const Expr*> args;
    const Expr* call = nullptr;  // anropsuttrycket (för livslängden av lån), om känt
    bool dispatch = false;       // virtuellt anrop som inte avgörs statiskt
  };

  struct Use {
    const Expr* e;  // nullptr för en fångst (då är path satt)
    Mode mode;
    std::optional<Path> path = std::nullopt;
  };

  // STN-75: arrayen i a[i], när a är en ägd array som namnges direkt: ett
  // fält, en variabel (lokal, referensparameter eller konstant tabell) eller
  // ett element i en ägd array (m[i][j]). Konverteringen till pekare godtas
  // bara här, som en del av indexeringen.
  const Expr* ownedArrayBase(const ArraySubscriptExpr& subscript) const {
    const auto* cast = dyn_cast<ImplicitCastExpr>(stripNoOp(subscript.getBase()));
    if (cast == nullptr || cast->getCastKind() != CK_ArrayToPointerDecay) return nullptr;
    const Expr* array = stripNoOp(cast->getSubExpr());
    return isOwnedArrayName(array) ? array : nullptr;
  }

  bool isOwnedArrayName(const Expr* e) const {
    e = stripNoOp(e);
    if (!e->isGLValue() || !types_.isOwnedArray(e->getType())) return false;
    if (const auto* m = dyn_cast<MemberExpr>(e)) return isa<FieldDecl>(m->getMemberDecl());
    if (const auto* dr = dyn_cast<DeclRefExpr>(e)) return isa<VarDecl>(dr->getDecl());
    if (const auto* inner = dyn_cast<ArraySubscriptExpr>(e)) return ownedArrayBase(*inner) != nullptr;
    return false;
  }

  std::optional<Path> pathOf(const Expr* e) const {
    e = stripNoOp(e);
    // STN-2: en bas (icke-virtuell) är en del av objektet; vägen är hela
    // objektet (överlappar allt i det).
    if (const auto* c = dyn_cast<ImplicitCastExpr>(e);
        c != nullptr && (c->getCastKind() == CK_UncheckedDerivedToBase || c->getCastKind() == CK_DerivedToBase))
      return pathOf(c->getSubExpr());
    if (const auto* dr = dyn_cast<DeclRefExpr>(e)) {
      if (const auto* binding = dyn_cast<BindingDecl>(dr->getDecl())) {
        auto it = bindingPaths_.find(binding);
        if (it == bindingPaths_.end()) return std::nullopt;
        Path p = it->second;
        p.text = binding->getNameAsString();
        return p;
      }
      const auto* vd = dyn_cast<VarDecl>(dr->getDecl());
      if (vd == nullptr || !vd->hasLocalStorage()) return std::nullopt;
      if (auto it = refs_.find(vd); it != refs_.end()) {  // lokal referens: dess mål
        Path p = it->second;
        p.text = vd->getNameAsString();
        return p;
      }
      return Path{vd, {}, vd->getNameAsString()};
    }
    if (isa<CXXThisExpr>(e)) return Path{nullptr, {}, "*this"};
    // STN-73: (a = b), (a += b): objektet själv.
    if (const auto* call = dyn_cast<CallExpr>(e)) {
      const FunctionDecl* f = call->getDirectCallee();
      const Expr* object = nullptr;
      if (const auto* mc = dyn_cast<CXXMemberCallExpr>(call)) object = mc->getImplicitObjectArgument();
      else if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(call); oc != nullptr && oc->getNumArgs() > 0 &&
               isa_and_nonnull<CXXMethodDecl>(f))
        object = oc->getArg(0);
      // STN-76: s[1] med en egen int& operator[]: en väg till hela objektet.
      if (f != nullptr && object != nullptr && (returnsThisChecked(*f) || returnsMemberRefChecked(*f)))
        return pathOf(object);
    }
    if (const auto* u = dyn_cast<UnaryOperator>(e);  // *this
        u != nullptr && u->getOpcode() == UO_Deref && isa<CXXThisExpr>(stripNoOp(u->getSubExpr())))
      return Path{nullptr, {}, "*this"};
    if (const auto* subscript = dyn_cast<ArraySubscriptExpr>(e)) {
      if (const Expr* base = ownedArrayBase(*subscript)) {
        auto p = pathOf(base);
        if (p) {
          p->open = true;
          p->text = exprText(e, p->text);
        }
        return p;
      }
    }
    // v[i], *o, o.value(): en okänd del av behållaren (öppen väg).
    if (const Expr* object = accessorObject(e)) {
      std::optional<Path> p = pathOf(object);
      if (!p) return std::nullopt;
      p->open = true;
      p->text = exprText(e, p->text);
      return p;
    }
    if (const auto* m = dyn_cast<MemberExpr>(e)) {
      const auto* field = dyn_cast<FieldDecl>(m->getMemberDecl());
      if (field == nullptr) return std::nullopt;
      const Expr* base = stripNoOp(m->getBase());
      // o.f där f ligger i en bas: samma objekt, fältet i basen (fältet är
      // en egen FieldDecl, så ett dolt namn är ett annat fält).
      while (const auto* c = dyn_cast<ImplicitCastExpr>(base)) {
        if (c->getCastKind() != CK_UncheckedDerivedToBase && c->getCastKind() != CK_DerivedToBase) break;
        base = stripNoOp(c->getSubExpr());
      }
      std::optional<Path> p;
      if (isa<CXXThisExpr>(base)) {
        p = Path{nullptr, {}, ""};
      } else if (m->isArrow()) {  // o->f: bara genom en accessor (operator->)
        const Expr* object = accessorObject(base);
        if (object == nullptr) return std::nullopt;
        p = pathOf(object);
        if (!p) return std::nullopt;
        p->open = true;
        p->text += "->";
      } else {
        p = pathOf(base);
        if (!p) return std::nullopt;
        p->text += ".";
      }
      if (!p->open) p->fields.push_back(field);
      p->text += field->getNameAsString();
      return p;
    }
    return std::nullopt;
  }

  // `p` skrivs: roten noteras (G4, fångster i lambdakroppar). Om ett lån in
  // i p lever avgör lånemodellen (LoanCheck, G2).
  void noteWrite(const Path& p) const {
    if (const auto* vd = dyn_cast_or_null<VarDecl>(p.root)) written_.insert(vd);
  }

  // Returvärdet i en funktion som returnerar `const T&`: en väg rotad i
  // *this eller i en const&-parameter (som överlever anropet), eller `c ? a : b`
  // där båda är det. Anroparen lånar objektet/argumentet (se declStmt).
  Reason refReturnSource(const Expr* e) const {
    if (e == nullptr) return std::string("return without value");
    e = stripNoOp(e);
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = stripNoOp(w->getSubExpr());
    if (const auto* c = dyn_cast<ConditionalOperator>(e)) {
      if (auto r = refReturnSource(c->getTrueExpr())) return r;
      return refReturnSource(c->getFalseExpr());
    }
    if (isTemporary(e) || e->isPRValue()) return std::string("returns a reference to a temporary");
    // `return at_index(i);`: en egen const-metod utan referensparametrar
    // pekar in i sitt objekt (bara här; som lokal referens är den ett lån, STN-33).
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(e);
        mc != nullptr && mc->getMethodDecl() != nullptr && returnsConstRefIntoThis(*mc->getMethodDecl()))
      e = mc->getImplicitObjectArgument();
    else if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e);
             oc != nullptr && oc->getNumArgs() > 0 && oc->getDirectCallee() != nullptr &&
             returnsConstRefIntoThis(*oc->getDirectCallee()))
      e = oc->getArg(0);
    // STN-99: en statisk lokal konstant lever till programmets slut.
    if (const auto* dr = dyn_cast<DeclRefExpr>(stripNoOp(e));
        dr != nullptr && constantStatics_.contains(dyn_cast<VarDecl>(dr->getDecl())))
      return std::nullopt;
    std::optional<Path> p = pathOf(e);
    if (!p) return std::string("returns a reference that is not a field of *this or a const& parameter");
    return outlivesCall(*p, "a reference");
  }

  // STN-76: returvärdet i en icke-const metod som ger T&: en väg in i *this,
  // eller `c ? a : b` där båda är det.
  Reason memberRefSource(const Expr* e) const {
    static const std::string kOnly = "a method returning a non-const reference may only return a reference into *this";
    if (e == nullptr) return std::string("return without value");
    e = stripNoOp(e);
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = stripNoOp(w->getSubExpr());
    if (const auto* c = dyn_cast<ConditionalOperator>(e)) {
      if (auto r = memberRefSource(c->getTrueExpr())) return r;
      return memberRefSource(c->getFalseExpr());
    }
    if (isTemporary(e) || e->isPRValue()) return std::string("returns a reference to a temporary");
    std::optional<Path> p = pathOf(e);
    if (!p || p->root != nullptr) return kOnly;
    // Genom ett referensfält (Ctx& ctx) är det ett annat objekt: en skrivning
    // genom resultatet syns inte i anroparens väg (G4).
    for (const FieldDecl* f : p->fields)
      if (f->getType()->isReferenceType())
        return "returns a reference through reference member '" + f->getNameAsString() + "'";
    return std::nullopt;
  }

  // Överlever det `p` pekar in i anropet? *this (bara const-metoder, se
  // checkFunction) och const&-parametrar gör det; lokala variabler och
  // värdeparametrar inte.
  static Reason outlivesCall(const Path& p, const std::string& what) {
    if (p.root == nullptr) return std::nullopt;
    const auto* pv = dyn_cast<ParmVarDecl>(p.root);
    const auto* ref = pv != nullptr ? pv->getType()->getAs<LValueReferenceType>() : nullptr;
    if (ref != nullptr && ref->getPointeeType().isConstQualified()) return std::nullopt;
    const auto* nd = dyn_cast<NamedDecl>(p.root);
    std::string into = what == "a reference" ? "a reference to '" : "a pointer into '";
    return "returns " + into + (nd != nullptr ? nd->getNameAsString() : p.text) +
           "', which does not outlive the call";
  }

  // Returvärdet i en funktion som returnerar `const T*`: ett uttryck som
  // klarar de vanliga reglerna. Vad pekaren lånar och att det överlever
  // anropet kontrolleras av lånemodellen (retur av lån av lokala variabler,
  // värdeparametrar och temporärer avvisas).
  Reason pointerReturnSource(const Expr* e) {
    if (e == nullptr) return std::string("return without value");
    return expr(e);
  }

  // Första icke-tomma orsaken, i ordning (till skillnad från all()).
  template <typename... F>
  static Reason inOrder(F... fs) {
    Reason out;
    ((out = out ? out : fs()), ...);
    return out;
  }

  // En init-sats/villkorsvariabel i en if/for/while/switch: lånen den
  // deklarerar lever i hela satsen (stmt() släpper dem vid satsens slut).
  Reason decls(const Stmt* s) { return s == nullptr ? std::nullopt : stmtImpl(s); }

  // Första icke-tomma orsaken. Argumenten utvärderas alla, i ospecificerad
  // ordning: bara för deluttryck som inte deklarerar något.
  template <typename... R>
  static Reason all(R... rs) {
    Reason out;
    ((out = out ? out : rs), ...);
    return out;
  }

  // Tilldelning av en värdetyp som är en ren kopia av värden: trivial, eller
  // implicit/defaulted (medlemsvis kopia av värdetyper).
  static bool isMemberwiseAssign(const CXXOperatorCallExpr* oc) {
    const auto* md = dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
    return md != nullptr && (md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator()) &&
           (md->isTrivial() || !md->isUserProvided());
  }

  // Kopierings-/flyttkonstruktor som är en ren kopia av värden.
  static bool isMemberwiseCopy(const CXXConstructorDecl& c) {
    return c.isCopyOrMoveConstructor() && (c.isTrivial() || !c.isUserProvided());
  }

  // Ett uttryck som står som egen sats (eller som for-loopens steg). Bara
  // här får den yttersta noden vara en sidoeffekt (++, --, =, op=, eller ett
  // anrop som ändrar sina argument); operanderna får inte ha egna
  // sidoeffekter. Samma regel som SPARK: inga sidoeffekter inuti uttryck.
  Reason exprStmt(const Expr* e) {
    if (e == nullptr) return std::nullopt;
    e = e->IgnoreParens();
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr()->IgnoreParens();
    if (const auto* b = dyn_cast<BinaryOperator>(e)) {
      if (b->getOpcode() == BO_Comma)  // sekvenserad: varje led är en "sats"
        return all(exprStmt(b->getLHS()), exprStmt(b->getRHS()));
      // STN-39: p += n och p -= n är pekararitmetik, som p + n.
      if (b->isCompoundAssignmentOp() && b->getLHS()->getType()->isPointerType())
        return "pointer arithmetic or ordering: " + b->getOpcodeStr().str();
      if (b->isAssignmentOp()) return assignment(b->getLHS(), b->getRHS());
    }
    if (const auto* u = dyn_cast<UnaryOperator>(e); u != nullptr && u->isIncrementDecrementOp()) {
      if (u->getSubExpr()->getType()->isPointerType())  // STN-39: ++p är pekararitmetik
        return std::string("pointer arithmetic or ordering: ") + (u->isIncrementOp() ? "++" : "--");
      if (auto r = written(u->getSubExpr())) return r;
      if (auto p = pathOf(u->getSubExpr())) noteWrite(*p);
      return std::nullopt;
    }
    if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e); oc != nullptr && isMemberwiseAssign(oc)) {
      // STN-68: en implicit tilldelning kan ersätta ägda värden (behållare).
      if (!oc->getDirectCallee()->isTrivial())
        if (auto r = recordUserCode(*oc->getDirectCallee())) return r;
      return assignment(oc->getArg(0), oc->getArg(1));
    }
    if (auto [el, x] = bitWrite(e); el != nullptr) return assignment(el, x);  // v[i] = b på vector<bool>
    if (const auto* c = dyn_cast<ExplicitCastExpr>(e); c != nullptr && c->getCastKind() == CK_ToVoid)
      return exprStmt(c->getSubExpr());  // (void)f(x): som f(x)
    // Ett anrop vars kastade resultat bara bär lån (en iterator, ett
    // pair<iterator, bool>): resultatet dör i satsen. Anropet granskas som
    // vanligt; dess låneeffekter kontrolleras av lånemodellen.
    if (const auto* call = dyn_cast<CallExpr>(e->IgnoreImplicit());
        call != nullptr && !types_.isValueType(call->getType()) && types_.isLoanValue(call->getType())) {
      auto parts = callParts(call);
      if (!parts) return parts.error();
      return callWith(*parts, true);
    }
    return effectAllowed(e);
  }

  // `lhs = rhs` (även op=): rhs får vara ett anrop med sidoeffekt, men dess
  // T&-argument får inte överlappa tilldelningsmålet.
  Reason assignment(const Expr* lhs, const Expr* rhs) {
    if (auto r = written(lhs)) return r;
    if (std::optional<Path> target = pathOf(lhs)) noteWrite(*target);
    markRead(rhs);  // a = v[i] för structar: rhs binds till const T&, ingen lvalue->rvalue
    Reason r = effectAllowed(rhs);
    return r;
  }

  // Vänsterledet i en tilldelning (eller operand till ++): genom en accessor
  // är det en skrivning till behållaren, som då måste vara en väg.
  Reason written(const Expr* lhs) {
    // STN-76: s[1] = 9 med en egen int& operator[]: anropet får ändra s (som
    // ändå skrivs), och målet är hela s.
    if (const auto* call = dyn_cast<CallExpr>(stripNoOp(lhs)); call != nullptr && call->getDirectCallee() != nullptr &&
                                                                returnsMemberRefChecked(*call->getDirectCallee())) {
      if (auto r = effectAllowed(lhs)) return r;
      if (!pathOf(lhs)) return "assignment through a method needs a variable or field as object";
      return std::nullopt;
    }
    if (accessorRoot(lhs) != nullptr) {
      markWrite(lhs);
      if (auto r = expr(lhs)) return r;
      if (!pathOf(lhs)) return "assignment through an accessor needs a variable or field as object";
      return std::nullopt;
    }
    return expr(lhs);
  }

  // Som effectAllowed, men i `c ? a : b` gäller det varje gren för sig: bara en
  // av dem körs, efter villkoret (STN-76: `return c ? x : v[0];`).
  Reason effectAllowedInBranches(const Expr* e) {
    const Expr* inner = stripNoOp(e);
    if (const auto* w = dyn_cast<ExprWithCleanups>(inner)) inner = stripNoOp(w->getSubExpr());
    if (const auto* c = dyn_cast<ConditionalOperator>(inner)) {
      if (auto r = expr(c->getCond())) return r;
      if (auto r = effectAllowedInBranches(c->getTrueExpr())) return r;
      return effectAllowedInBranches(c->getFalseExpr());
    }
    return effectAllowed(e);
  }

  // Granska e där den yttersta noden (genom genomskinliga omslag som
  // implicita casts och temporärer) får vara ett anrop med sidoeffekt.
  Reason effectAllowed(const Expr* e) {
    effectOk_ = true;
    Reason r = expr(e);
    effectOk_ = false;
    return r;
  }

  // Default-initiering med den triviala standardkonstruktorn lämnar fälten
  // oinitierade (`Vec2 v;`). Värdeinitiering (`Vec2 v{}`, `Vec2()`) nollar.
  static bool isUninitializedConstruct(const Expr* e) {
    const auto* ce = dyn_cast<CXXConstructExpr>(e);
    return ce != nullptr && ce->getConstructor()->isDefaultConstructor() &&
           ce->getConstructor()->isTrivial() && !ce->requiresZeroInitialization();
  }

  // En skalär utan initierare i den analyserade funktionen (inte i en lambda:
  // analysen gäller bara funktionens egna variabler).
  bool mayBeUnassigned(const VarDecl& vd) const {
    if (analysed_ == nullptr || vd.getInit() != nullptr || vd.getDeclContext() != analysed_) return false;
    QualType t = vd.getType();
    return !t->isReferenceType() && !t->isPointerType() &&
           (t->isIntegralOrEnumerationType() || t->isRealFloatingType());
  }

  // En variabel utan initierare som binds till en referens: clangs analys
  // räknar ett T&-argument som initiering, så det underkänns alltid.
  Reason boundUnassigned(const Path& p) const {
    if (const auto* vd = dyn_cast_or_null<VarDecl>(p.root); vd != nullptr && unassigned_.contains(vd))
      return "local variable '" + vd->getNameAsString() + "' without initializer is bound to a reference";
    return std::nullopt;
  }

  static bool isInitialized(const VarDecl& vd) {
    const Expr* init = vd.getInit();
    return init != nullptr && !isUninitializedConstruct(init->IgnoreImplicit());
  }

  // Namnet i meddelanden; den dolda variabeln i `auto [a, b] = e;` heter `[a, b]`.
  static std::string localName(const VarDecl& vd) {
    const auto* dd = dyn_cast<DecompositionDecl>(&vd);
    if (dd == nullptr) return vd.getNameAsString();
    std::string out = "[";
    for (const BindingDecl* b : dd->bindings()) out += (out.size() > 1 ? ", " : "") + b->getNameAsString();
    return out + "]";
  }

  Reason declStmt(const DeclStmt* ds) {
    for (const Decl* d : ds->decls()) {
      if (isa<TypedefNameDecl>(d)) continue;  // typedef/using-alias: ofarligt
      if (isa<StaticAssertDecl>(d)) continue;  // STN-64: bara kompileringstid
      // STN-81: `using std::swap;`, `using namespace std;`: bara namnuppslagning;
      // anropen granskas med den funktion clang valde.
      if (isa<UsingDecl, UsingDirectiveDecl>(d)) continue;
      const auto* vd = dyn_cast<VarDecl>(d);
      if (vd == nullptr)
        return std::string("unsupported local declaration: ") + d->getDeclKindName();
      // STN-75: en statisk lokal konstant (`static constexpr int t[3] = {...};`)
      // initieras av kompilatorn och ändras aldrig: som en konstant global.
      // Bara med trivial destruktor (inget körs vid programmets slut).
      if (!vd->hasLocalStorage()) {
        QualType t = vd->getType();
        bool constant = !t->isReferenceType() && !staticVariable(*vd) &&
                        (types_.isValueType(t) || types_.isOwnedArray(t)) &&
                        t.isDestructedType() == QualType::DK_none;
        // STN-99: en const värdetyp med konstant initiering (en tom std::vector
        // som returneras när inget annat finns): ingen kod vid första anropet,
        // aldrig ändrad. Destruktorn körs vid programmets slut.
        bool sentinel = !constant && !t->isReferenceType() && t.isConstQualified() &&
                        types_.isValueType(t) && vd->hasConstantInitialization();
        if (!constant && !sentinel) return "local variable '" + localName(*vd) + "' has static storage";
        if (sentinel) {
          if (auto r = destroyed(t)) return r;
          if (auto r = expr(vd->getInit())) return r;
          constantStatics_.insert(vd);
        }
        continue;
      }
      if (vd->hasAttr<CleanupAttr>())  // STN-14: en funktion körs vid blockets slut
        return "local variable '" + localName(*vd) + "' has a cleanup function";
      if (auto r = destroyed(vd->getType())) return r;  // STN-68: vid blockets slut
      if (const CXXRecordDecl* rd = vd->getType()->getAsCXXRecordDecl(); rd != nullptr && rd->isLambda()) {
        if (auto r = lambdaVariable(*vd)) return r;
        continue;
      }
      // STN-71: en initializer_list bara som parameter. Som lokal variabel
      // kan dess temporära array dö före den (`l = {3, 4};`).
      if (TypeRules::initializerListElement(vd->getType()))
        return "local variable '" + localName(*vd) + "' has type " + types_.typeName(vd->getType()) +
               " (std::initializer_list is only allowed as a parameter)";
      // Lånevärde (iterator, vy, pair med en iterator): lånemodellen följer
      // lånen; här granskas bara initieraren som vanligt.
      if (!vd->getType()->isReferenceType() && !types_.isValueType(vd->getType()) &&
          types_.isLoanValue(vd->getType())) {
        if (!isInitialized(*vd)) return "local variable '" + localName(*vd) + "' is not initialized";
        initializing_ = vd;
        Reason r = effectAllowed(vd->getInit());
        initializing_ = nullptr;
        if (r) return r;
        continue;
      }
      // STN-75: en lokal ägd array ska vara initierad i deklarationen; varje
      // element granskas (också de som fylls i implicit).
      if (vd->getType()->isConstantArrayType()) {
        if (auto w = types_.whyNotOwnedArray(vd->getType()))
          return "local variable '" + localName(*vd) + "' has type " + types_.typeName(vd->getType()) + *w;
        if (vd->getInit() == nullptr) return "local variable '" + localName(*vd) + "' is not initialized";
        initializing_ = vd;
        Reason r = arrayElements(vd->getInit());
        initializing_ = nullptr;
        if (r) return r;
        continue;
      }
      // Lokal referens: T& eller const T& till en värdetyp eller en ägd array
      // (se bindReference).
      QualType value = vd->getType();
      const auto* ref = value->getAs<LValueReferenceType>();
      if (ref != nullptr) value = ref->getPointeeType();
      if (auto w = ref != nullptr && value->isConstantArrayType() ? types_.whyNotOwnedArray(value)
                                                                  : types_.whyNotValue(value))
        return "local variable '" + localName(*vd) + "' has type " +
               types_.typeName(vd->getType()) + *w;
      if (!isInitialized(*vd)) {
        if (!mayBeUnassigned(*vd)) return "local variable '" + localName(*vd) + "' is not initialized";
        if (maybeUnassigned_.contains(vd))
          return "local variable '" + localName(*vd) + "' may be read before it is assigned";
        unassigned_.insert(vd);
        continue;
      }
      // `int& v = vec[1].age_;`: accessorn i roten är en skrivning till
      // behållaren (en väg), som `vec[1].age_ = 3;` (STN-37), inte ett anrop
      // med sidoeffekt inuti initieraren.
      if (ref != nullptr && !value.isConstQualified()) {
        const Expr* init = vd->getInit();
        if (const auto* w = dyn_cast<ExprWithCleanups>(init)) init = w->getSubExpr();
        init = stripNoOp(init);
        if (accessorObject(init) == nullptr && accessorRoot(init) != nullptr && pathOf(init)) markWrite(init);
      }
      initializing_ = vd;
      Reason r = effectAllowed(vd->getInit());
      initializing_ = nullptr;
      if (r) return r;
      if (ref != nullptr)
        if (auto b = bindReference(*vd)) return b;
      if (const auto* dd = dyn_cast<DecompositionDecl>(vd))
        if (auto b = decomposition(*dd)) return b;
    }
    return std::nullopt;
  }

  // STN-84: `auto [a, b] = e;`. Den dolda variabeln (dd) är granskad ovan som
  // en vanlig lokal, kopia eller referens. Varje bindning blir en väg in i
  // den: ett fält (`dd.x`, clangs bindningsuttryck) eller, för tuple-lika
  // typer, resultatet av std::get på dd (ett okänt element av dd, som i
  // range-for över std::map). Inget annat anrop godtas i en hållare.
  Reason decomposition(const DecompositionDecl& dd) {
    // STN-84: `auto&& [a, b]` avvisas, också när det deduceras till T& (lvärde).
    if (const TypeSourceInfo* ts = dd.getTypeSourceInfo(); ts != nullptr && ts->getType()->isReferenceType())
      if (!ts->getTypeLoc().getUnqualifiedLoc().getAs<RValueReferenceTypeLoc>().isNull() ||
          !ts->getType()->getAs<ReferenceType>()->isSpelledAsLValue())
        return "structured binding with auto&&";
    for (const BindingDecl* binding : dd.bindings()) {
      std::string name = "structured binding '" + binding->getNameAsString() + "'";
      if (auto w = types_.whyNotValue(binding->getType()))
        return name + " has type " + types_.typeName(binding->getType()) + *w;
      std::optional<Path> path;
      if (const VarDecl* holding = binding->getHoldingVar()) {
        const Expr* init = holding->getInit();
        const auto* get = dyn_cast_or_null<CallExpr>(init ? init->IgnoreImplicit() : nullptr);
        const FunctionDecl* callee = get ? get->getDirectCallee() : nullptr;
        if (callee == nullptr || !inSystemHeader(*callee) || callee->getQualifiedNameAsString() != "std::get")
          return name + " does not use std::get";
        const Expr* object = nullptr;
        if (const auto* mc = dyn_cast<CXXMemberCallExpr>(get)) object = mc->getImplicitObjectArgument();
        else if (get->getNumArgs() == 1) object = get->getArg(0);
        const auto* dr = object ? dyn_cast<DeclRefExpr>(stripNoOp(object)) : nullptr;
        if (dr == nullptr || dr->getDecl() != &dd) return name + " does not name the decomposed object";
        if (auto r = recordCallee(*callee)) return r;
        path = pathOf(dr);
        if (path) path->open = true;
      } else if (const Expr* e = binding->getBinding(); e != nullptr && isa<MemberExpr>(stripNoOp(e))) {
        path = pathOf(e);
      } else {
        return name + " is not a field or a std::get element";
      }
      if (!path) return name + " has no known target";
      path->text = binding->getNameAsString();
      bindingPaths_[binding] = *path;
    }
    return std::nullopt;
  }

  // Vad en lokal referens binds till:
  //  - en temporär: livslängden förlängs, referensen är sin egen rot,
  //  - en väg: referensen är ett annat namn för vägen,
  //  - ett anrop som returnerar en referens och har exakt ett
  //    referensargument/objekt, som är en väg: ett lån av den vägen.
  Reason bindReference(const VarDecl& vd) {
    std::string name = vd.getNameAsString();
    const Expr* init = vd.getInit();
    if (const auto* w = dyn_cast<ExprWithCleanups>(init)) init = w->getSubExpr();
    init = stripNoOp(init);
    if (isa<MaterializeTemporaryExpr>(init)) return std::nullopt;
    if (auto p = pathOf(init); p && accessorObject(init) == nullptr) {
      if (auto r = boundUnassigned(*p)) return r;
      refs_[&vd] = *p;
      return std::nullopt;
    }
    // STN-36: en icke-const referens ska ha ett känt mål, annars syns inte
    // skrivningarna genom den (G4, `written_`). En const-referens som inte
    // kan lösas upp är sin egen rot: genom den läses bara. Om den kan dingla
    // eller dö avgör lånemodellen (G2).
    const bool readOnly = vd.getType()->getPointeeType().isConstQualified();
    auto reject = [&](std::string why) -> Reason {
      if (readOnly) return std::nullopt;
      return why;
    };
    std::string unbound = "local reference '" + name +
                          "' must be bound to a variable, a field, a temporary or an element";
    auto parts = callParts(init);
    if (!parts) return reject(unbound);
    const Expr* source = nullptr;
    // Ett std-anrop vars resultat enligt sammanfattningarna bara lånar
    // objektet (map::at, map::operator[], vector::front, std::get ...), inte
    // nyckel- eller värdeargumenten. Sidoeffekterna kontrollerades i declStmt.
    if (inSystemHeader(*parts->callee) && types_.borrowsOnlyFirst(*parts->callee) &&
        (parts->object != nullptr || !parts->args.empty())) {
      source = parts->object != nullptr ? parts->object : parts->args.front();
    } else {
      for (const Use& u : usesOf(*parts)) {
        if (u.mode == Mode::value) continue;
        if (source != nullptr) return reject("local reference '" + name + "' may refer to more than one object");
        source = u.e;
      }
    }
    if (source == nullptr) return reject(unbound);
    if (isTemporary(source)) return reject("local reference '" + name + "' may dangle (bound through a temporary)");
    auto container = pathOf(source);
    if (!container) return reject(unbound);
    Path target = *container;
    target.open = true;
    refs_[&vd] = target;
    return std::nullopt;
  }

  Reason expr(const Expr* e) {
    // Får just den här noden vara ett anrop med sidoeffekt? Genomskinliga
    // omslag skickar rätten vidare (`pass`); alla andra deluttryck får den inte.
    bool effectHere = std::exchange(effectOk_, false);
    if (effectGranted_.erase(e) != 0) effectHere = true;  // STN-85, se eraseRemove
    auto pass = [&](const Expr* sub) {
      effectOk_ = effectHere;
      return expr(sub);
    };

    if (e == nullptr) return std::nullopt;
    if (lambdaArgs_.contains(e)) return std::nullopt;  // STN-79, se lambdaArguments
    if (const auto* dr = dyn_cast<DeclRefExpr>(e)) {
      if (const auto* vd = dyn_cast<VarDecl>(dr->getDecl()); vd != nullptr && lambdas_.contains(vd))
        return "lambda '" + vd->getNameAsString() + "' can only be called";
    }

    // Generellt skydd: varje uttryck måste ha värdetyp (eller void, för
    // anrop av void-funktioner). Fångar bl.a. pekare/referenser/klasser som
    // smyger in via uttryck vi annars skulle släppa igenom. (Uttryck har
    // aldrig referenstyp i clang; en referensparameter syns som ett lvalue
    // av typen T.)
    // vector<bool>: proxyn från v[i] släpps igenom här; den känns bara igen som
    // läsning (operator bool) eller skrivning (= bool), se VectorBool.hpp.
    if (const Expr* el = bitRead(e)) {
      markRead(el);
      return expr(el);
    }
    // Ett glvalue av objekttyp (en port, *this, en parameter T&) får användas
    // som objekt eller referensargument; en kopia av det är en konstruktor och
    // underkänns där.
    if (!e->getType()->isVoidType() && !types_.isValueType(e->getType()) && bitElement(e) == nullptr &&
        !returnsLambdaArgument(e) &&
        !(e->isGLValue() && !types_.whyNotObject(e->getType())) && !isLoanValueExpr(e) &&
        !isOwnedArrayName(e) &&
        !e->getType()->isNullPtrType())
      return std::string("expression of type ") + types_.typeName(e->getType()) + " (" +
             e->getStmtClassName() + ")";

    if (isa<IntegerLiteral, FloatingLiteral, CXXBoolLiteralExpr, CharacterLiteral,
            ImplicitValueInitExpr, CXXScalarValueInitExpr>(e))
      return std::nullopt;
    if (isa<CXXNullPtrLiteralExpr>(e)) return std::nullopt;  // nullptr (som pekare, se castExpr)
    // sizeof/alignof: en konstant; operanden utvärderas inte. Inte för VLA
    // (då är värdet inte konstant).
    if (const auto* u = dyn_cast<UnaryExprOrTypeTraitExpr>(e)) {
      bool sizeOrAlign = u->getKind() == UETT_SizeOf || u->getKind() == UETT_AlignOf ||
                         u->getKind() == UETT_PreferredAlignOf;
      if (sizeOrAlign && u->getIntegerConstantExpr(types_.context())) return std::nullopt;
      return std::string("unsupported expression: ") + e->getStmtClassName();
    }
    if (const auto* p = dyn_cast<ParenExpr>(e)) return pass(p->getSubExpr());
    // a != b i C++20 utan egen operator!=: clang skriver om till !(a == b)
    // (eller b == a). Den omskrivna formen är koden.
    if (const auto* rw = dyn_cast<CXXRewrittenBinaryOperator>(e)) return pass(rw->getSemanticForm());
    // STN-80: ett consteval-anrop (`x < 0` på std::strong_ordering bygger
    // std::__cmp_cat::__literal_zero) har körts av kompilatorn: resultatet är
    // en konstant, inget körs. Bara för ett prvalue av aritmetisk typ, enum
    // eller tom klass: en pekare eller ett fält kunde peka in i statisk lagring.
    if (const auto* c = dyn_cast<ConstantExpr>(e)) {
      if (c->isImmediateInvocation() && c->isPRValue() && constantValue(c->getType())) return std::nullopt;
      return pass(c->getSubExpr());
    }
    // STN-65: mallparameter i en instans = det insatta värdet.
    if (const auto* t = dyn_cast<SubstNonTypeTemplateParmExpr>(e)) return pass(t->getReplacement());
    if (const auto* d = dyn_cast<CXXDefaultArgExpr>(e)) return expr(d->getExpr());
    // STN-71: en klamrad lista som blir en std::initializer_list. Elementen
    // granskas som argument; lånemodellen följer den temporära arrayen.
    if (const auto* il = dyn_cast<CXXStdInitializerListExpr>(e)) {
      const auto* array = dyn_cast<InitListExpr>(il->getSubExpr()->IgnoreImplicit());
      if (array == nullptr || !array->getType()->isConstantArrayType())
        return "unsupported initializer_list backing array";
      return args(array->inits());
    }
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(e)) {
      // STN-66: en standardinitierare är en del av konstruktionen, som en
      // initierare i konstruktorn (`std::span<const char> v{t};` lånar ett
      // annat fält: en kopia lånar originalet, STN-92).
      if (auto r = expr(d->getExpr())) return r;
      if (usesThis(d->getExpr())) return "initializer of field '" + d->getField()->getNameAsString() + "' uses *this";
      return std::nullopt;
    }
    // Temporärer av värdetyp: destruktorn är trivial, implicit (medlemsvis)
    // eller del av en betrodd typ.
    if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) return pass(m->getSubExpr());
    if (const auto* w = dyn_cast<ExprWithCleanups>(e)) return pass(w->getSubExpr());
    if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) {  // STN-68: temporären förstörs
      if (const CXXDestructorDecl* d = b->getTemporary()->getDestructor())
        if (auto r = recordUserCode(*d)) return r;
      return pass(b->getSubExpr());
    }

    if (const auto* ice = dyn_cast<ImplicitCastExpr>(e)) return castExpr(ice, pass);
    if (isa<CStyleCastExpr, CXXStaticCastExpr, CXXFunctionalCastExpr>(e))
      return castExpr(llvm::cast<CastExpr>(e), [&](const Expr* sub) { return expr(sub); });

    if (const auto* dr = dyn_cast<DeclRefExpr>(e)) return declRef(dr);
    if (const auto* m = dyn_cast<MemberExpr>(e)) {
      // STN-97: ett fält i temporären från det yttersta anropet
      // (`m.emplace(k, v).second`) läses efter att anropet körts en gång.
      if (effectHere && !m->isArrow() && isa<FieldDecl>(m->getMemberDecl()) && isTemporary(m->getBase()))
        return pass(m->getBase());
      return member(m);
    }
    if (const auto* subscript = dyn_cast<ArraySubscriptExpr>(e))
      if (const Expr* base = ownedArrayBase(*subscript)) return all(expr(base), expr(subscript->getIdx()));

    if (const auto* il = dyn_cast<InitListExpr>(e)) {
      for (const Expr* init : il->inits())
        if (auto r = arrayFieldInit(il, init)) return *r;
        else if (auto r2 = expr(init)) return r2;
      return std::nullopt;
    }
    if (const auto* pl = dyn_cast<CXXParenListInitExpr>(e)) {
      for (const Expr* init : pl->getInitExprs())
        if (auto r = expr(init)) return r;
      return std::nullopt;
    }
    if (const auto* ce = dyn_cast<CXXConstructExpr>(e)) {
      const CXXConstructorDecl* ctor = ce->getConstructor();
      if (isUninitializedConstruct(ce))
        return "default-initialized " + recordName(*ctor->getParent()) +
               " (fields not initialized)";
      if (ctor->isTrivial() || isMemberwiseCopy(*ctor)) {
        // STN-72: en implicit/defaulted kopiering kör fältens egna
        // kopieringsfunktioner (och std::optional<T>(const optional&) T:s).
        if (!ctor->isTrivial())
          if (auto r = recordUserCode(*ctor)) return r;
        for (const Expr* a : ce->arguments()) markRead(a);
        return args(ce->arguments());
      }
      // Implicit standardkonstruktor i egen kod: fältinitierarna granskas.
      // I en systemheader (std::vector<T>{}) är den betrodd som andra medlemmar.
      if (ctor->isDefaultConstructor() && !ctor->isUserProvided() &&
          !ctor->getASTContext().getSourceManager().isInSystemHeader(ctor->getLocation())) {
        // STN-66/67: den implicita konstruktorn kör fältens och basernas egna
        // konstruktorer (`Outer o;` med ett fält vars standardkonstruktor är egen).
        if (auto r = recordUserCode(*ctor)) return r;
        return defaultInit(*ctor->getParent());
      }
      return callWith(CallParts{ctor, nullptr, false, Mode::value, argList(ce->arguments()), ce},
                      effectHere);
    }

    if (const auto* u = dyn_cast<UnaryOperator>(e)) {
      switch (u->getOpcode()) {
        case UO_Plus: case UO_Minus: case UO_Not: case UO_LNot:
          return expr(u->getSubExpr());
        case UO_Deref:
          if (isa<CXXThisExpr>(stripNoOp(u->getSubExpr()))) return std::nullopt;  // *this
          // *p för en pekare (ett lånevärde): lånemodellen följer vad p pekar på.
          if (types_.isLoanValue(u->getSubExpr()->getType())) return expr(u->getSubExpr());
          return "unsupported unary operator: *";
        case UO_AddrOf:
          // &x: ett lån av x (lånemodellen). Inte av en temporär eller funktion.
          if (u->getSubExpr()->isGLValue() && types_.isLoanValue(u->getType())) return expr(u->getSubExpr());
          return "unsupported unary operator: &";
        case UO_PreInc: case UO_PreDec: case UO_PostInc: case UO_PostDec:
          return std::string("side effect inside expression: ") +
                 UnaryOperator::getOpcodeStr(u->getOpcode()).str();
        default:
          return std::string("unsupported unary operator: ") +
                 UnaryOperator::getOpcodeStr(u->getOpcode()).str();
      }
    }
    if (const auto* b = dyn_cast<BinaryOperator>(e)) {
      if (b->isPtrMemOp()) return "unsupported binary operator: pointer-to-member";
      if (b->isAssignmentOp())  // =, +=, -= osv. (bara tillåtna som egen sats)
        return "side effect inside expression: " + b->getOpcodeStr().str();
      // STN-18/20: komma bara som kedja av satser (exprStmt), inte i ett uttryck.
      // Operanderna först: deras skäl (en sidoeffekt) säger mer.
      if (b->getOpcode() == BO_Comma) {
        if (auto r = all(expr(b->getLHS()), expr(b->getRHS()))) return r;
        return std::string("comma operator inside an expression");
      }
      // Pekare: bara == och != (nullptr eller samma objekt). Aritmetik och
      // ordning (<, >) är inte lån utan adresser.
      if ((b->getLHS()->getType()->isPointerType() || b->getRHS()->getType()->isPointerType()) &&
          !b->isEqualityOp())
        return "pointer arithmetic or ordering: " + b->getOpcodeStr().str();
      return all(expr(b->getLHS()), expr(b->getRHS()));
    }
    if (const auto* co = dyn_cast<ConditionalOperator>(e))
      return all(expr(co->getCond()), expr(co->getTrueExpr()), expr(co->getFalseExpr()));

    if (preCondition(e) != nullptr) return "PORK_PRE must come before all other statements";
    if (postCondition(e) != nullptr) return "PORK_POST must come before all other statements";
    if (flowRequirement(e) != nullptr) return "PORK_DEPENDS must come before all other statements";
    if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e); oc != nullptr && oc->getOperator() == OO_Call) {
      if (const VarDecl* f = lambdaVar(oc->getArg(0))) {
        const auto* op = dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
        if (op == nullptr) return "indirect lambda call";
        std::vector<const Expr*> rest = argList(oc->arguments());
        rest.erase(rest.begin());
        return lambdaCall(*op, *lambdas_.at(f), rest, effectHere, "lambda '" + f->getNameAsString() + "'");
      }
    }
    if (const auto* c = dyn_cast<CallExpr>(e); c != nullptr && e->getStmtClass() == Stmt::CallExprClass &&
                                                c->getDirectCallee() != nullptr &&
                                                c->getDirectCallee()->getQualifiedNameAsString() == "std::visit")
      return visit(*c, effectHere);
    if (const Expr* a = moveArg(e)) return movedFrom(e, a);
    if (isa<CXXMemberCallExpr, CXXOperatorCallExpr>(e) || e->getStmtClass() == Stmt::CallExprClass) {
      // Medlemsvis tilldelning (a = b för structar) är en sidoeffekt: bara
      // tillåten som egen sats (se exprStmt).
      if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e); oc != nullptr && isMemberwiseAssign(oc))
        return "side effect inside expression: =";
      auto parts = callParts(e);
      if (!parts) return parts.error();
      return callWith(*parts, effectHere);
    }

    return std::string("unsupported expression: ") + e->getStmtClassName();
  }

  // Standardinitiering via implicit konstruktor: varje fält har en
  // initierare (granskas som rent uttryck), är en klass med egen sådan
  // initiering, eller en betrodd typ (systemheader).
  // Använder uttrycket *this (fält, metoder, this), även via standardinitierare
  // och standardargument?
  static bool usesThis(const Stmt* s) {
    if (s == nullptr) return false;
    if (isa<CXXThisExpr>(s)) return true;
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(s)) return usesThis(d->getExpr());
    if (const auto* d = dyn_cast<CXXDefaultArgExpr>(s)) return usesThis(d->getExpr());
    for (const Stmt* c : s->children())
      if (usesThis(c)) return true;
    return false;
  }

  // STN-66 (c): som usesThis, men läsning av ett aritmetiskt fält eller
  // enumfält i klassen `rd` som redan är initierat (`initialized`), via
  // implicit this (`y(x + 1)`), räknas inte: det är en kopia av ett värde.
  static bool usesThisExceptInitialized(const Stmt* s, const CXXRecordDecl& rd,
                                        const std::set<const FieldDecl*>& initialized) {
    if (s == nullptr) return false;
    if (const auto* c = dyn_cast<ImplicitCastExpr>(s); c != nullptr && c->getCastKind() == CK_LValueToRValue)
      if (const auto* m = dyn_cast<MemberExpr>(c->getSubExpr()->IgnoreParens()))
        if (const auto* t = dyn_cast<CXXThisExpr>(m->getBase()); t != nullptr && t->isImplicit())
          if (const auto* f = dyn_cast<FieldDecl>(m->getMemberDecl());
              f != nullptr && f->getParent() == &rd && initialized.contains(f) &&
              (f->getType()->isArithmeticType() || f->getType()->isEnumeralType()))
            return false;
    if (isa<CXXThisExpr>(s)) return true;
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(s)) return usesThis(d->getExpr());
    if (const auto* d = dyn_cast<CXXDefaultArgExpr>(s)) return usesThis(d->getExpr());
    for (const Stmt* c : s->children())
      if (usesThisExceptInitialized(c, rd, initialized)) return true;
    return false;
  }

  // Bas- eller delegerande initierare: anropet till konstruktorn (STN-66).
  // Inte via expr(): uttrycket har basens typ, som kan vara en objekttyp.
  Reason constructorCall(const CXXConstructExpr& ce) {
    const CXXConstructorDecl* ctor = ce.getConstructor();
    if (ctor->isTrivial()) return args(ce.arguments());
    if (ctor->isDefaultConstructor() && !ctor->isUserProvided() &&
        !ctor->getASTContext().getSourceManager().isInSystemHeader(ctor->getLocation())) {
      if (auto r = recordUserCode(*ctor)) return r;  // STN-66/67, som ovan
      return defaultInit(*ctor->getParent());
    }
    return callWith(CallParts{ctor, nullptr, false, Mode::value, argList(ce.arguments()), &ce}, true);
  }

  // En implicit standardkonstruktor: varje delobjekt (baser och fält, F1 i
  // den adversariella granskningen) ska få ett värde.
  Reason defaultInit(const CXXRecordDecl& rd) {
    for (const Subobject& o : subobjects(rd)) {
      std::string what = o.field != nullptr ? "field '" + o.field->getNameAsString() + "'"
                                             : "base " + types_.typeName(o.type());
      if (o.field != nullptr && o.field->hasInClassInitializer()) {
        const Expr* init = o.field->getInClassInitializer();
        if (types_.isOwnedArray(o.field->getType())) {
          if (usesThis(init)) return "initializer of " + what + " uses *this";
          if (auto r = arrayElements(init)) return r;
        } else if (auto r = expr(init)) {
          return r;
        }
        if (usesThis(init)) return "initializer of " + what + " uses *this";
        continue;
      }
      // STN-75: ett arrayfält utan initierare initieras element för element,
      // som ett fält av elementtypen.
      const CXXRecordDecl* sub = o.type()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
      if (sub != nullptr && sub->getASTContext().getSourceManager().isInSystemHeader(sub->getLocation()))
        continue;
      if (sub != nullptr && sub->hasDefinition() && !sub->hasTrivialDefaultConstructor()) {
        if (auto r = defaultInit(*sub->getDefinition())) return r;
        continue;
      }
      if (sub != nullptr && !hasFields(o.type())) continue;  // tom: inget att initiera
      return what + " of " + recordName(rd) + " is not initialized";
    }
    return std::nullopt;
  }

  template <typename Range>
  static std::vector<const Expr*> argList(Range range) {
    std::vector<const Expr*> out;
    for (const Expr* a : range) out.push_back(a);
    return out;
  }

  template <typename Range>
  Reason args(Range range) {
    for (const Expr* arg : range)
      if (auto r = expr(arg)) return r;
    return std::nullopt;
  }

  // Objektets läge i ett metodanrop. En icke-const metod i en systemheader
  // som summaries/std.txt säger bara läser (reads/access), och vars resultat
  // bara läses, räknas som const: `*o + 1`, `v[i].x` där `o`/`v` inte är const
  // (STN-37). Inte för att det finns en const-överlagring: det vore en gissning.
  Mode objectMode(const CXXMethodDecl& md, const Expr* call) const {
    if (md.isConst()) return Mode::constRef;
    return readCalls_.contains(call) ? Mode::constRef : Mode::mutRef;
  }

  // Accessor: anrop till en instansmetod i en systemheader som är const eller
  // som std.txt säger bara läser, och returnerar en referens eller (operator->) en pekare
  // in i objektet: v[i], v.at(i), *o, o.value(), o->. Objektet, annars nullptr.
  const Expr* accessorObject(const Expr* e) const {
    e = stripNoOp(e);
    const CXXMethodDecl* md = nullptr;
    const Expr* object = nullptr;
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(e)) {
      md = mc->getMethodDecl();
      object = mc->getImplicitObjectArgument();
    } else if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e)) {
      md = dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
      if (md != nullptr && oc->getNumArgs() > 0) object = oc->getArg(0);
    } else if (const auto* fc = dyn_cast<CallExpr>(e); fc != nullptr && fc->getNumArgs() == 1) {
      // std::get<I>(t) ([tuple.elem], [variant.get]): ett fritt std-anrop som
      // ger T& in i sitt enda argument, med access i std.txt, som t.first.
      const FunctionDecl* fd = fc->getDirectCallee();
      if (fd == nullptr || isa<CXXMethodDecl>(fd) || !fd->getReturnType()->isLValueReferenceType()) return nullptr;
      if (!fd->getASTContext().getSourceManager().isInSystemHeader(fd->getLocation())) return nullptr;
      if (!fc->getArg(0)->isLValue() || !types_.libraryReads(*fd)) return nullptr;
      return fc->getArg(0);
    }
    if (md == nullptr || !md->isInstance() || object == nullptr) return nullptr;
    QualType rt = md->getReturnType();
    if (!rt->isLValueReferenceType() && !(md->getOverloadedOperator() == OO_Arrow && rt->isPointerType()) &&
        !isVectorBoolProxyAccessor(*md))  // v[i] på vector<bool>: proxyn räknas som elementet
      return nullptr;
    if (!md->getASTContext().getSourceManager().isInSystemHeader(md->getLocation())) return nullptr;
    if (!md->isConst() && !types_.libraryReads(*md)) return nullptr;
    return object;
  }

  // Text för en accessor-väg i meddelanden: "v[...]", "*o", "o.value()".
  static std::string exprText(const Expr* e, const std::string& object) {
    e = stripNoOp(e);
    if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e)) {
      if (oc->getOperator() == OO_Subscript) return object + "[...]";
      if (oc->getOperator() == OO_Star) return "*" + object;
    }
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(e))
      return object + "." + mc->getMethodDecl()->getNameAsString() + "(...)";
    if (const auto* fc = dyn_cast<CallExpr>(e); fc != nullptr && fc->getDirectCallee() != nullptr)
      return fc->getDirectCallee()->getQualifiedNameAsString() + "(" + object + ")";
    return object;
  }

  // Roten i a.b[i].c / o->f / *o: accessor-anropet, eller nullptr.
  const Expr* accessorRoot(const Expr* e) const {
    while (true) {
      e = stripNoOp(e);
      if (accessorObject(e) != nullptr) return e;
      if (const auto* m = dyn_cast<MemberExpr>(e)) {
        e = m->getBase();
        continue;
      }
      // STN-75: v[k].data[i] är en del av elementet v[k], som v[k].n.
      if (const auto* s = dyn_cast<ArraySubscriptExpr>(e))
        if (const Expr* base = ownedArrayBase(*s)) {
          e = base;
          continue;
        }
      if (const auto* c = dyn_cast<ImplicitCastExpr>(e); c != nullptr && c->getCastKind() == CK_LValueToRValue) {
        e = c->getSubExpr();
        continue;
      }
      return nullptr;
    }
  }

  // `e` läses (lvalue -> rvalue, kopia, const&-argument): ett accessor-anrop
  // i roten räknas som const (se objectMode).
  void markRead(const Expr* e) {
    if (const Expr* root = accessorRoot(e)) readCalls_.insert(stripNoOp(root));
  }

  // `e` skrivs (tilldelning, ++, T&-argument). Ett accessor-anrop i roten är
  // en skrivning till behållaren; den spåras som en väg (pathOf), så själva
  // anropet räknas inte som en egen sidoeffekt.
  void markWrite(const Expr* e) { markRead(e); }

  template <typename Pass>
  Reason castExpr(const CastExpr* c, Pass pass) {
    if (c->getCastKind() == CK_LValueToRValue) markRead(c->getSubExpr());
    switch (c->getCastKind()) {
      case CK_LValueToRValue: case CK_NoOp:
      case CK_IntegralCast: case CK_IntegralToBoolean: case CK_IntegralToFloating:
      case CK_FloatingToIntegral: case CK_FloatingToBoolean: case CK_FloatingCast:
      case CK_BooleanToSignedIntegral:
      case CK_PointerToBoolean: case CK_NullToPointer:  // if (p), p == nullptr
      case CK_ToVoid:  // inuti ett uttryck: operanden granskas som vanligt
      // Implicit konvertering via konstruktor/konverteringsfunktion: själva
      // anropet ligger under och granskas som ett anrop.
      case CK_ConstructorConversion: case CK_UserDefinedConversion:
      // STN-2: en icke-virtuell bas är en del av objektet (vägen: pathOf).
      case CK_UncheckedDerivedToBase: case CK_DerivedToBase:
        return pass(c->getSubExpr());
      default:
        return std::string("unsupported cast: ") + c->getCastKindName();
    }
  }

  // Ett värde utan adresser: inga referenser eller pekare, inte heller i fält,
  // baser eller element. En konstant som håller en adress ändras aldrig själv,
  // men det den pekar på kan vara en föränderlig global (STN-22).
  static bool holdsNoAddress(QualType t) {
    t = t.getCanonicalType();
    if (t->isReferenceType() || t->isAnyPointerType() || t->isMemberPointerType()) return false;
    if (const auto* at = dyn_cast<ArrayType>(t.getTypePtr())) return holdsNoAddress(at->getElementType());
    const auto* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr) return true;
    if (!rd->hasDefinition()) return false;
    rd = rd->getDefinition();
    for (const CXXBaseSpecifier& b : rd->bases())
      if (!holdsNoAddress(b.getType())) return false;
    for (const FieldDecl* f : rd->fields())
      if (!holdsNoAddress(f->getType())) return false;
    return true;
  }

  static Reason staticVariable(const VarDecl& vd) {
    if (vd.hasLocalStorage()) return std::nullopt;
    if (!holdsNoAddress(vd.getType()))
      return "uses variable '" + vd.getQualifiedNameAsString() + "' with static storage";
    if (vd.isConstexpr()) return std::nullopt;  // kompileringstidskonstant
    // `static const size_type npos = -1;` (std::string::npos): const och
    // konstant initierad, alltså en kompileringstidskonstant.
    // En out-of-line-definition kan sakna initierare även när den
    // tidigare deklarationen i klassen har en (t.ex. libstdc++:s npos).
    const VarDecl* init = nullptr;
    if (vd.getType().isConstQualified() && vd.getAnyInitializer(init) != nullptr &&
        init->isUsableInConstantExpressions(vd.getASTContext()) && init->evaluateValue() != nullptr)
      return std::nullopt;
    // STN-75: `const int table[] = {...};` är en const-array med konstant
    // initiering (ingen kod vid start) som aldrig ändras.
    if (vd.getType()->isConstantArrayType() && vd.getType().isConstant(vd.getASTContext()) &&
        vd.getAnyInitializer(init) != nullptr && init->hasConstantInitialization())
      return std::nullopt;
    return "uses variable '" + vd.getQualifiedNameAsString() + "' with static storage";
  }

  Reason declRef(const DeclRefExpr* dr) {
    const ValueDecl* d = dr->getDecl();
    if (isa<ParmVarDecl>(d)) return std::nullopt;  // typen kollad på funktionsnivå
    if (isa<EnumConstantDecl>(d)) return std::nullopt;
    if (const auto* binding = dyn_cast<BindingDecl>(d); binding != nullptr && bindingPaths_.contains(binding))
      return std::nullopt;
    if (const auto* vd = dyn_cast<VarDecl>(d)) {
      if (vd == initializing_)
        return "local variable '" + vd->getNameAsString() + "' is used in its own initializer";
      if (constantStatics_.contains(vd)) return std::nullopt;  // STN-99
      return staticVariable(*vd);
    }
    return "unsupported reference to " + d->getQualifiedNameAsString() + " (" +
           d->getDeclKindName() + ")";
  }

  // En metod på en std-typ märkt [[gsl::Pointer]], eller en fri std-operator
  // vars parametrar alla är sådana (iteratorjämförelser).
  static bool stdLoanOperation(const FunctionDecl& fd) {
    if (!fd.getASTContext().getSourceManager().isInSystemHeader(fd.getLocation())) return false;
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd); md != nullptr && md->isInstance())
      return TypeRules::isGslPointerRecord(*md->getParent());
    if (fd.getOverloadedOperator() == OO_None || fd.getNumParams() == 0) return false;
    for (const ParmVarDecl* p : fd.parameters()) {
      const CXXRecordDecl* rd = p->getType().getNonReferenceType()->getAsCXXRecordDecl();
      if (rd == nullptr || !TypeRules::isGslPointerRecord(*rd)) return false;
    }
    return true;
  }

  // Ett uttryck av lånevärdestyp (prvalue eller glvalue).
  bool isLoanValueExpr(const Expr* e) { return types_.isLoanValue(e->getType()); }

  // Objektet i a.f / this->f / a.m(). `this` godtas: methodReason har redan
  // krävt att klassen är en värdetyp.
  Reason object(const Expr* base, bool arrow) {
    if (isa<CXXThisExpr>(base->IgnoreParenImpCasts())) return std::nullopt;
    if (arrow && types_.isLoanValue(base->getType())) return expr(base);  // p->f för en pekare
    if (arrow) {
      // o->f med en betrodd operator-> är (*o).f: anropet granskas som läsning.
      const Expr* call = stripNoOp(base);
      if (accessorObject(call) == nullptr) return "member access through pointer";
      readCalls_.insert(call);
      auto parts = callParts(call);
      if (!parts) return parts.error();
      return callWith(*parts, false);
    }
    return expr(base);
  }

  Reason member(const MemberExpr* m) {
    const ValueDecl* d = m->getMemberDecl();
    if (const auto* vd = dyn_cast<VarDecl>(d))  // statisk datamedlem
      return all(object(m->getBase(), m->isArrow()), staticVariable(*vd));
    if (!isa<FieldDecl>(d))
      return "unsupported member access: " + d->getQualifiedNameAsString();
    return object(m->getBase(), m->isArrow());
  }

  // Ett dispatchande anrop (virtuellt, inte avvirtualiserat) kan nå vilken
  // override som helst, även utanför de analyserade källorna. Det registreras
  // under en egen nyckel ("dispatch:" + usr) som aldrig har en definition, och
  // betros: anroparen får antagandet "dispatch I::f".
  Reason recordCallee(const FunctionDecl& callee, bool dispatch = false) {
    // Operationer på std-iteratorer och -vyer ([[gsl::Pointer]]: *, ->, ++,
    // ==, ...) registreras inte, som i range-for: deras låneeffekter står i
    // std-sammanfattningarna och kontrolleras av lånemodellen.
    if (stdLoanOperation(callee)) return std::nullopt;
    std::string usr = usrOf(callee);
    if (usr.empty()) return "calls '" + callee.getNameAsString() + "' (no USR)";
    if (dispatch) {
      callees_.emplace(std::string(kDispatchPrefix) + usr, Callee{displayName(callee), "dispatch " + trustName(callee), true, ""});
      return std::nullopt;
    }
    callees_.emplace(usr, Callee{displayName(callee), trustName(callee),
                                 hasAnnotation(callee, "pork::trusted"), trustAlias(callee)});
    // STN-67: egen kod som std-funktionen kör är också anrop.
    if (types_.context().getSourceManager().isInSystemHeader(callee.getLocation())) return recordUserCode(callee);
    return std::nullopt;
  }

 public:
  // STN-67/68: de egna funktioner som f kör (f själv om den är skriven av
  // användaren, annars det som dess kropp, implicita medlemmar och std når).
  Reason recordUserCode(const FunctionDecl& f) {
    for (const FunctionDecl* user : userCode_.from(f)) {
      std::string u = usrOf(*user);
      if (u.empty()) return "calls '" + user->getNameAsString() + "' (no USR)";
      callees_.emplace(u, Callee{displayName(*user), trustName(*user), hasAnnotation(*user, "pork::trusted"), trustAlias(*user)});
    }
    return std::nullopt;
  }

  // STN-68: ett objekt av typen t förstörs här.
  Reason destroyed(QualType t) {
    if (t.isNull() || t->isReferenceType()) return std::nullopt;
    t = t.getCanonicalType();
    while (const auto* a = dyn_cast<ArrayType>(t.getTypePtr())) t = a->getElementType().getCanonicalType();
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition()) return std::nullopt;
    const CXXDestructorDecl* d = rd->getDefinition()->getDestructor();
    if (d == nullptr || d->isTrivial()) return std::nullopt;
    return recordUserCode(*d);
  }

  // STN-68: fälten och baserna förstörs efter en destruktors kropp.
  Reason destroyedMembers(const CXXRecordDecl& rd) {
    for (const Subobject& o : subobjects(rd))
      if (auto r = destroyed(o.type())) return r;
    return std::nullopt;
  }

 private:

  // Kan användarkod som `callee` (i std) når skriva ett objekt av typen `t`?
  // En icke-const metod i t:s klass (en konverteringsfunktion), eller en
  // funktion med en icke-const referensparameter till t.
  bool userCodeMayWrite(const FunctionDecl& callee, QualType t) const {
    QualType key = t.getCanonicalType().getUnqualifiedType();
    const CXXRecordDecl* rd = key->getAsCXXRecordDecl();
    UserCodeScan scan(types_.context().getSourceManager());  // egen: userCode_ minns det redan sedda
    for (const FunctionDecl* f : scan.from(callee)) {
      if (const auto* md = dyn_cast<CXXMethodDecl>(f);
          md != nullptr && rd != nullptr && md->isInstance() && !md->isConst() &&
          md->getParent()->getCanonicalDecl() == rd->getCanonicalDecl())
        return true;
      for (const ParmVarDecl* p : f->parameters()) {
        const auto* r = p->getType()->getAs<ReferenceType>();
        if (r == nullptr || r->getPointeeType().isConstQualified()) continue;
        QualType pt = r->getPointeeType().getCanonicalType().getUnqualifiedType();
        if (pt == key || pt->isTemplateTypeParmType()) return true;
      }
    }
    return false;
  }

  // Forwarding-referens (U&& där U är funktionens templateparameter) i en
  // funktion från en systemheader: behandlas som en läsning. Det ingår i att
  // lita på den funktionen (t.ex. std::optional(U&&), value_or(U&&)).
  static bool isTrustedForwarding(const FunctionDecl& callee, unsigned i) {
    if (!callee.getASTContext().getSourceManager().isInSystemHeader(callee.getLocation()))
      return false;
    const FunctionDecl* pattern = callee.getTemplateInstantiationPattern();
    if (pattern == nullptr || i >= pattern->getNumParams()) return false;
    const auto* rr = pattern->getParamDecl(i)->getType()->getAs<RValueReferenceType>();
    return rr != nullptr && !rr->getPointeeType().hasQualifiers() &&
           rr->getPointeeType()->getAs<TemplateTypeParmType>() != nullptr;
  }

  // STN-25: en forwarding-referens i std har instansens typ (U&& med U = T&
  // blir T&) och kan skriva (std::ranges::sort(R&&), std::invoke), utom i en
  // std-konstruktor (den bygger sitt objekt av argumentet: en kopia av ett
  // lvalue, eller en vy som lånemodellen följer) och där std.txt säger att
  // funktionen bara läser (reads/access).
  Mode paramMode(const FunctionDecl& callee, unsigned i) const {
    if (i >= callee.getNumParams()) return Mode::value;  // C-variadiskt
    const auto* ref = callee.getParamDecl(i)->getType()->getAs<ReferenceType>();
    if (ref == nullptr) return Mode::value;
    if (ref->getPointeeType().isConstQualified()) return Mode::constRef;
    // P-d: en std-konstruktor bygger *this av sitt forwarding-argument (en
    // kopia, eller en vy som lånemodellen följer): bibliotekets egen effekt.
    // Användarkod som konstruktorn når (T(U&), U::operator T()) kan skriva
    // argumentet, och den avgör: då är det en skrivning.
    if (isTrustedForwarding(callee, i) && isa<CXXConstructorDecl>(callee) &&
        !userCodeMayWrite(callee, ref->getPointeeType()))
      return Mode::constRef;
    return Mode::mutRef;
  }

  // Dela upp ett anrop. Bara rena funktionsanrop, medlemsanrop och
  // operatoranrop; övriga subklasser av CallExpr underkänns. Callee-uttrycket
  // (DeclRefExpr + FunctionToPointerDecay, eller MemberExpr) granskas inte;
  // bara objekt och argument.
  std::expected<CallParts, std::string> callParts(const Expr* e) const {
    auto parts = callPartsOf(e);
    if (parts) parts->call = e;
    return parts;
  }

  std::expected<CallParts, std::string> callPartsOf(const Expr* e) const {
    if (const auto* mc = dyn_cast<CXXMemberCallExpr>(e)) {
      const CXXMethodDecl* md = mc->getMethodDecl();
      const auto* callee = dyn_cast<MemberExpr>(mc->getCallee()->IgnoreParens());
      if (md == nullptr || callee == nullptr) return std::unexpected("indirect member call");
      CallParts parts{md, callee->getBase(), callee->isArrow(), objectMode(*md, e), argList(mc->arguments())};
      parts.dispatch = isDispatch(*mc, *md, callee->getBase());
      return parts;
    }
    if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e)) {
      const FunctionDecl* callee = oc->getDirectCallee();
      if (callee == nullptr)
        return std::unexpected(std::string("unsupported operator call: ") +
                               getOperatorSpelling(oc->getOperator()));
      std::vector<const Expr*> all = argList(oc->arguments());
      // Medlemsoperator: argument 0 är objektet.
      if (const auto* md = dyn_cast<CXXMethodDecl>(callee); md != nullptr && !md->isStatic()) {
        const Expr* object = all.front();
        all.erase(all.begin());
        CallParts parts{md, object, false, objectMode(*md, e), all};
        parts.dispatch = isDispatch(*oc, *md, object);
        return parts;
      }
      return CallParts{callee, nullptr, false, Mode::value, all};
    }
    if (e->getStmtClass() == Stmt::CallExprClass) {
      const auto* c = llvm::cast<CallExpr>(e);
      const FunctionDecl* callee = c->getDirectCallee();
      if (callee == nullptr) return std::unexpected("indirect call");
      return CallParts{callee, nullptr, false, Mode::value, argList(c->arguments())};
    }
    return std::unexpected(std::string("not a call: ") + e->getStmtClassName());
  }

  // Argument i är en strängliteral till en `const char*`-parameter
  // (StringLiteralArg.hpp): konstant data, ingen användning av något.
  static bool literalArg(const CallParts& c, unsigned i) {
    if (i >= c.callee->getNumParams()) return false;
    if (stringLiteralArg(c.args[i], c.callee->getParamDecl(i)->getType()) != nullptr) return true;
    // std::pair vidarebefordrar ofta literalen som const char(&)[N].
    // Godta det för en ägd std::string, med parkonstruktorn som antagande.
    const auto* literal = dyn_cast<StringLiteral>(c.args[i]->IgnoreParenImpCasts());
    const auto* ctor = dyn_cast<CXXConstructorDecl>(c.callee);
    if (literal == nullptr || !literal->isOrdinary() || ctor == nullptr || i >= 2 ||
        trustName(*ctor) != "std::pair::pair" || !inSystemHeader(*ctor))
      return false;
    const auto* pair = dyn_cast<ClassTemplateSpecializationDecl>(ctor->getParent());
    if (pair == nullptr) return false;
    const auto* str = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        pair->getTemplateArgs()[i].getAsType()->getAsCXXRecordDecl());
    return str != nullptr && str->getSpecializedTemplate()->getQualifiedNameAsString() == "std::basic_string" &&
           str->getTemplateArgs()[0].getAsType()->isCharType();
  }

  std::vector<Use> usesOf(const CallParts& c) const {
    std::vector<Use> uses;
    if (c.object != nullptr) uses.push_back({c.object, c.objectMode});
    for (unsigned i = 0; i < c.args.size(); ++i) {
      if (literalArg(c, i)) continue;
      // std::begin(v) som v.begin() (STN-78): det fria anropets behållare läses.
      Mode m = i == 0 && c.object == nullptr && c.call != nullptr && readCalls_.contains(c.call)
                   ? Mode::constRef
                   : paramMode(*c.callee, i);
      uses.push_back({c.args[i], m});
    }
    return uses;
  }

  // std::move(x): x (en väg) ska börja i en lokal variabel eller en
  // värdeparameter, och roten får inte vara levande efter anropet: efter en
  // move är värdet giltigt men ospecificerat, och ingen får läsa det.
  Reason movedFrom(const Expr* call, const Expr* arg) {
    std::optional<Path> p = pathOf(arg);
    // STN-90: en flyttad std::unique_ptr är tom ([unique.ptr.single.ctor],
    // [unique.ptr.single.asgn]: u.get() == nullptr), och lånemodellen följer
    // tomheten (STN-89). Vilken väg som helst får flyttas och användas efteråt;
    // i samma fullständiga uttryck får ingen överlappande väg förekomma.
    if (p && isUniquePtrType(arg->getType())) {
      noteWrite(*p);
      markWrite(arg);  // v[0], v.front(): skrivningen är flytten, som i `v[0] = x`
      if (auto r = sameExpression(call, *p, true)) return r;
      return expr(arg);
    }
    const auto* root = p ? dyn_cast_or_null<VarDecl>(p->root) : nullptr;
    const auto* parm = dyn_cast_or_null<ParmVarDecl>(root);
    bool moveSource = parm != nullptr && TypeRules::isMoveSource(*parm);  // STN-72: rhs.x i T(T&& rhs)
    if (root == nullptr || (root->getType()->isReferenceType() && !moveSource) || !root->hasLocalStorage())
      return std::string("std::move of something other than a local variable or value parameter");
    noteWrite(*p);
    // STN-100: för en väg (bara fält) räcker att inget överlappande förekommer.
    bool pathMove = !p->fields.empty() && !p->open;
    if (auto r = sameExpression(call, *p, moveSource || pathMove)) return r;
    if (moveSource) {
      if (auto r = movedFromSource(call, arg, *p, *parm)) return r;
      return expr(arg);
    }
    // STN-102: destruktorerna som körs när roten dör ser det flyttade fältet.
    if (auto r = destructorsMayRead(*p, root->getType())) return r;
    auto it = liveAfter_.find(call);
    if (it == liveAfter_.end())
      return "cannot tell whether '" + root->getNameAsString() + "' is used after std::move";
    bool live = it->second.contains(root);
    const VarDecl* liveRef = nullptr;
    for (const auto& [var, path] : refs_)  // en referens in i roten som används efteråt
      if (path.root == root && it->second.contains(var)) liveRef = var;
    // STN-96: lambdor som fångar roten med referens räknas inte av
    // LiveVariables; sökningen nedan ser anropen av dem.
    if (!live && liveRef == nullptr && !capturedByReference(root)) return expr(arg);
    if (p->open) {  // v[i].f: vilket element är okänt, roten ska vara död
      if (live) return "'" + root->getNameAsString() + "' is used after std::move(" + p->text + ")";
      if (liveRef != nullptr) return "'" + liveRef->getNameAsString() + "' is used after std::move(" + p->text + ")";
    }
    if (auto r = usedAfterMove(call, *p)) return r;
    return expr(arg);
  }

  // STN-96: finns en lokal lambda som fångar v med referens?
  bool capturedByReference(const VarDecl* v) const {
    if (moveFunction_ == nullptr || !moveFunction_->hasBody()) return false;
    bool found = false;
    std::function<void(const Stmt*)> walk = [&](const Stmt* s) {
      if (s == nullptr || found) return;
      if (const auto* l = dyn_cast<LambdaExpr>(s))
        for (const LambdaCapture& c : l->captures())
          if (c.capturesVariable() && c.getCaptureKind() == LCK_ByRef && c.getCapturedVar() == v) found = true;
      for (const Stmt* c : s->children()) walk(c);
    };
    walk(moveFunction_->getBody());
    return found;
  }

  // En lokal variabel vars typ är en lambda som fångar root med referens:
  // att använda den (anropa, kopiera) använder root.
  bool closureOf(const ValueDecl* d, const Decl* root) const {
    const auto* vd = dyn_cast<VarDecl>(d);
    const CXXRecordDecl* rd = vd ? vd->getType().getNonReferenceType()->getAsCXXRecordDecl() : nullptr;
    if (rd == nullptr || !rd->isLambda()) return false;
    for (const LambdaCapture& c : rd->captures())
      if (c.capturesVariable() && c.getCaptureKind() == LCK_ByRef && c.getCapturedVar() == root) return true;
    return false;
  }

  const CFG* moveCfg() {
    if (moveCfg_ != nullptr || moveFunction_ == nullptr || !moveFunction_->hasBody()) return moveCfg_;
    moveCfgManager_ = std::make_unique<AnalysisDeclContextManager>(const_cast<ASTContext&>(types_.context()));
    moveCfgManager_->getCFGBuildOptions().setAllAlwaysAdd();
    moveCfgManager_->getCFGBuildOptions().AddInitializers = true;
    AnalysisDeclContext* ac = moveCfgManager_->getContext(moveFunction_);
    if (ac == nullptr || ac->getCFG() == nullptr) return nullptr;
    restoreCoveredSwitchDefaults(*ac->getCFG());
    moveCfg_ = ac->getCFG();
    return moveCfg_;
  }

  // STN-96: std::move(p) där roten lever vidare. Från flytten framåt i CFG:n
  // (alla vägar) får ingen väg som överlappar p förekomma (p, något i p, ett
  // objekt som rymmer p, en referens eller lambda som når dem) förrän p eller
  // något som rymmer p tilldelas helt, eller roten deklareras på nytt. Når
  // sökningen flytten igen är den i en loop utan ny tilldelning.
  Reason usedAfterMove(const Expr* call, const Path& moved) {
    const auto* root = cast<VarDecl>(moved.root);
    const std::string what = "std::move(" + moved.text + ")";
    const CFG* cfg = moveCfg();
    if (cfg == nullptr) return "cannot tell whether '" + root->getNameAsString() + "' is used after std::move";
    std::set<const DeclRefExpr*> own;
    std::function<void(const Stmt*)> mark = [&](const Stmt* s) {
      if (s == nullptr) return;
      if (const auto* dr = dyn_cast<DeclRefExpr>(s)) own.insert(dr);
      for (const Stmt* c : s->children()) mark(c);
    };
    mark(call);
    std::vector<std::pair<const CFGBlock*, unsigned>> todo;
    for (const CFGBlock* b : *cfg)
      for (unsigned i = 0; i < b->size(); ++i)
        if (auto cs = (*b)[i].getAs<CFGStmt>(); cs && cs->getStmt() == call) todo.push_back({b, i + 1});
    if (todo.empty()) return "cannot tell whether '" + root->getNameAsString() + "' is used after std::move";
    std::map<const DeclRefExpr*, bool> seen;  // true = tilldelning som avslutar sökningen
    std::set<const CFGBlock*> visited;
    while (!todo.empty()) {
      auto [b, i] = todo.back();
      todo.pop_back();
      bool stop = false;
      for (; i < b->size() && !stop; ++i) {
        const Stmt* s = nullptr;
        if (auto cs = (*b)[i].getAs<CFGStmt>()) s = cs->getStmt();
        else if (auto ci = (*b)[i].getAs<CFGInitializer>()) s = ci->getInitializer()->getInit();
        if (s == nullptr) continue;
        if (s == call) return what + " in a loop";
        if (const auto* ds = dyn_cast<DeclStmt>(s); ds != nullptr && llvm::is_contained(ds->decls(), root)) {
          stop = true;  // en ny variabel (loopkroppen)
          break;
        }
        if (auto r = usesAfterMove(s, moved, own, seen, stop)) return r;
      }
      if (stop) continue;
      for (const CFGBlock::AdjacentBlock& succ : b->succs())
        if (const CFGBlock* n = succ.getReachableBlock(); n != nullptr && visited.insert(n).second)
          todo.push_back({n, 0});
    }
    return std::nullopt;
  }

  // STN-102: för varje typ som rymmer den flyttade vägen (roten, sedan fälten)
  // och dess baser: en egen destruktor får inte kunna läsa resten av vägen.
  Reason destructorsMayRead(const Path& moved, QualType rootType) const {
    const std::string what = "std::move(" + moved.text + ")";
    for (std::size_t i = 0; i < moved.fields.size(); ++i) {
      // Typen längs vägen, och klassen som äger fältet: efter ett index
      // (v[0].namn) är typen behållaren, men elementets destruktor körs också.
      QualType t = i == 0 ? rootType : moved.fields[i - 1]->getType();
      std::vector<const FieldDecl*> rest(moved.fields.begin() + static_cast<std::ptrdiff_t>(i), moved.fields.end());
      std::vector<const CXXRecordDecl*> todo;
      const CXXRecordDecl* owners[] = {t.getNonReferenceType()->getAsCXXRecordDecl(), dyn_cast<CXXRecordDecl>(moved.fields[i]->getParent())};
      for (const CXXRecordDecl* rd : owners)
        if (rd != nullptr && rd->hasDefinition()) todo.push_back(rd->getDefinition());
      if (todo.empty()) continue;
      const CXXRecordDecl* rd = todo.front();
      while (!todo.empty()) {
        const CXXRecordDecl* r = todo.back();
        todo.pop_back();
        if (const CXXDestructorDecl* d = r->getDestructor(); d != nullptr && d->isUserProvided() && mayRead(*d, rest))
          return what + " out of " + recordName(*rd->getDefinition()) + ", whose destructor may read it";
        for (const CXXBaseSpecifier& b : r->bases())
          if (const CXXRecordDecl* base = b.getType()->getAsCXXRecordDecl(); base && base->hasDefinition())
            todo.push_back(base->getDefinition());
      }
    }
    return std::nullopt;
  }

  // Kan destruktorn d läsa vägen `rest` i *this? Varje `this` ska vara basen i
  // en kedja av fält; kedjan får inte överlappa rest (den ena ett prefix av den
  // andra). Annan användning av this (metodanrop, *this som argument) och en
  // destruktor utan kropp räknas som läsning.
  bool mayRead(const CXXDestructorDecl& d, const std::vector<const FieldDecl*>& rest) const {
    const FunctionDecl* def = nullptr;
    if (!d.hasBody(def) || def->getBody() == nullptr) return true;
    auto& ctx = const_cast<ASTContext&>(types_.context());
    bool found = false;
    std::function<void(const Stmt*)> walk = [&](const Stmt* s) {
      if (s == nullptr || found) return;
      if (isa<CXXThisExpr>(s)) {
        std::vector<const FieldDecl*> chain;
        const Stmt* cur = s;
        for (;;) {
          auto parents = ctx.getParents(*cur);
          const Stmt* up = parents.size() == 1 ? parents[0].get<Stmt>() : nullptr;
          if (const auto* ic = dyn_cast_or_null<ImplicitCastExpr>(up);
              ic != nullptr && (ic->getCastKind() == CK_UncheckedDerivedToBase || ic->getCastKind() == CK_DerivedToBase ||
                                ic->getCastKind() == CK_NoOp)) {
            cur = up;
            continue;
          }
          const auto* me = dyn_cast_or_null<MemberExpr>(up);
          const auto* f = me ? dyn_cast<FieldDecl>(me->getMemberDecl()) : nullptr;
          if (f == nullptr) break;
          chain.push_back(f);
          cur = up;
        }
        if (chain.empty()) {
          found = true;  // metodanrop eller *this
          return;
        }
        std::size_t n = std::min(chain.size(), rest.size());
        found = std::equal(chain.begin(), chain.begin() + static_cast<std::ptrdiff_t>(n), rest.begin());
        return;
      }
      for (const Stmt* c : s->children()) walk(c);
    };
    walk(def->getBody());
    return found;
  }

  // Förekomster i s av vägar som överlappar den flyttade. `stop` sätts vid en
  // tilldelning till den flyttade vägen eller något som rymmer den.
  Reason usesAfterMove(const Stmt* s, const Path& moved, const std::set<const DeclRefExpr*>& own,
                       std::map<const DeclRefExpr*, bool>& seen, bool& stop) {
    Reason found;
    std::function<void(const Stmt*)> walk = [&](const Stmt* x) {
      if (x == nullptr || found || stop) return;
      if (const auto* dr = dyn_cast<DeclRefExpr>(x); dr != nullptr && !own.contains(dr)) {
        if (auto it = seen.find(dr); it != seen.end()) {
          if (it->second) stop = true;
          return;
        }
        bool kill = false;
        found = useAfterMove(dr, moved, own, kill);
        seen[dr] = kill;
        if (kill) stop = true;
        return;
      }
      if (const auto* l = dyn_cast<LambdaExpr>(x)) walk(l->getBody());
      for (const Stmt* c : x->children()) walk(c);
    };
    walk(s);
    return found;
  }

  Reason useAfterMove(const DeclRefExpr* dr, const Path& moved, const std::set<const DeclRefExpr*>& own, bool& kill) {
    const std::string what = "std::move(" + moved.text + ")";
    const ValueDecl* d = dr->getDecl();
    if (closureOf(d, moved.root)) return "'" + d->getNameAsString() + "' is used after " + what;
    bool relevant = d == moved.root;
    if (const auto* vd = dyn_cast<VarDecl>(d))
      if (auto it = refs_.find(vd); it != refs_.end() && it->second.root == moved.root) relevant = true;
    if (const auto* bd = dyn_cast<BindingDecl>(d)) {
      if (auto it = bindingPaths_.find(bd); it != bindingPaths_.end()) {
        relevant = relevant || it->second.root == moved.root;
      } else if (const auto* dd = dyn_cast_or_null<VarDecl>(bd->getDecomposedDecl())) {
        auto ref = refs_.find(dd);
        relevant = relevant || dd == moved.root || (ref != refs_.end() && ref->second.root == moved.root);
      }
    }
    if (!relevant) return std::nullopt;
    const Expr* top = largestPath(dr);
    std::optional<Path> q = pathOf(top);
    if (!q) return "'" + d->getNameAsString() + "' is used after " + what;
    // v[i].f: vilket element är okänt, så varje väg i roten överlappar.
    if (!moved.open && !q->overlaps(moved)) return std::nullopt;
    bool whole = !q->open && (moved.open ? q->fields.empty() : q->fields.size() <= moved.fields.size());
    if (whole && resetCall(top)) {  // STN-101: `p.namn.clear()`
      kill = true;
      return std::nullopt;
    }
    if (const Expr* rhs = whole ? assignedFrom(top) : nullptr) {
      std::map<const DeclRefExpr*, bool> inner;
      bool ignored = false;
      if (auto r = usesAfterMove(rhs, moved, own, inner, ignored)) return r;
      kill = true;
      return std::nullopt;
    }
    return "'" + q->text + "' is used after " + what;
  }

  // STN-101: är e objektet i ett anrop utan argument som std.txt märker `resets`?
  bool resetCall(const Expr* e) const {
    auto& ctx = const_cast<ASTContext&>(types_.context());
    auto parents = ctx.getParents(*e);
    const auto* me = parents.size() == 1 ? parents[0].get<MemberExpr>() : nullptr;
    if (me == nullptr || stripNoOp(me->getBase()) != e) return false;
    auto up = ctx.getParents(*me);
    const auto* mc = up.size() == 1 ? up[0].get<CXXMemberCallExpr>() : nullptr;
    const CXXMethodDecl* md = mc ? mc->getMethodDecl() : nullptr;
    return md != nullptr && mc->getNumArgs() == 0 && types_.libraryResets(*md);
  }

  // Är e målet för en hel tilldelning (`e = x`: inbyggd, eller operator= som
  // är implicit/defaulted eller från std)? Då högerledet, annars nullptr.
  const Expr* assignedFrom(const Expr* e) const {
    auto& ctx = const_cast<ASTContext&>(types_.context());
    auto parents = ctx.getParents(*e);
    const Expr* up = parents.size() == 1 ? parents[0].get<Expr>() : nullptr;
    if (const auto* bo = dyn_cast_or_null<BinaryOperator>(up);
        bo != nullptr && bo->getOpcode() == BO_Assign && stripNoOp(bo->getLHS()) == e)
      return bo->getRHS();
    const auto* oc = dyn_cast_or_null<CXXOperatorCallExpr>(up);
    if (oc == nullptr || oc->getOperator() != OO_Equal || oc->getNumArgs() != 2 || stripNoOp(oc->getArg(0)) != e)
      return nullptr;
    const auto* md = dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
    if (md == nullptr) return nullptr;
    bool system = ctx.getSourceManager().isInSystemHeader(md->getLocation());
    if (!system && md->isUserProvided()) return nullptr;
    return oc->getArg(1);
  }

  // STN-57/72: ordningen mellan operander och argument är ospecificerad, så
  // varken källordningen (movedFromSource) eller CFG:n (liveAfter_) säger om
  // en annan förekomst i samma fullständiga uttryck körs före flytten
  // (`add(*p, take(std::move(p)))`, g++ räknar från höger). Där får roten inte
  // förekomma alls, direkt eller genom en lokal referens; för en flyttkälla
  // (STN-72) ingen väg som överlappar den flyttade.
  Reason sameExpression(const Expr* call, const Path& moved, bool source) {
    auto& ctx = const_cast<ASTContext&>(types_.context());
    const Stmt* top = call;
    for (;;) {
      auto parents = ctx.getParents(*top);
      const Expr* up = parents.size() == 1 ? parents[0].get<Expr>() : nullptr;
      if (up == nullptr) break;
      top = up;
    }
    std::optional<std::string> found;
    std::function<void(const Stmt*)> walk = [&](const Stmt* s) {
      if (s == nullptr || s == call || found) return;
      if (const auto* dr = dyn_cast<DeclRefExpr>(s)) {
        if (dr->getDecl() == moved.root) {
          if (!source) {
            found = dr->getDecl()->getNameAsString();
            return;
          }
          std::optional<Path> p = pathOf(largestPath(dr));
          if (!p || p->overlaps(moved)) found = p ? p->text : dr->getDecl()->getNameAsString();
          return;
        }
        if (const auto* vd = dyn_cast<VarDecl>(dr->getDecl()))
          if (auto it = refs_.find(vd); it != refs_.end() && it->second.root == moved.root) {
            found = vd->getNameAsString();
            return;
          }
      }
      for (const Stmt* c : s->children()) walk(c);
    };
    walk(top);
    if (found) return "'" + *found + "' is used in the same expression as std::move(" + moved.text + ")";
    return std::nullopt;
  }

  // Den största vägen från en DeclRefExpr: o.f.g, inte bara o.
  const Expr* largestPath(const DeclRefExpr* dr) const {
    auto& ctx = const_cast<ASTContext&>(types_.context());
    const Expr* top = dr;
    for (;;) {
      auto parents = ctx.getParents(*top);
      const Expr* up = parents.size() == 1 ? parents[0].get<Expr>() : nullptr;
      const auto* me = dyn_cast_or_null<MemberExpr>(up);
      const auto* cast = dyn_cast_or_null<ImplicitCastExpr>(up);
      if (isa_and_nonnull<ParenExpr>(up) || (cast != nullptr && cast->getCastKind() == CK_NoOp)) {
        top = up;
        continue;
      }
      if (me == nullptr || !isa<FieldDecl>(me->getMemberDecl())) break;
      top = up;
    }
    return top;
  }

  // STN-72: std::move(o.f) ur källan o i T(T&& o) / operator=(T&& o). Fält
  // för fält är idiomet (`a(std::move(o.a)), b(std::move(o.b))`), så här
  // gäller vägar och inte roten: ingen väg som överlappar o.f (o själv, o.f,
  // o.f.g) får förekomma efter flytten. Ordningen är utvärderingsordningen:
  // initierarna i konstruktorns ordning (deklarationsordningen), sedan
  // kroppen; inom en del källordningen. En flytt i en loop underkänns.
  Reason movedFromSource(const Expr* call, const Expr* arg, const Path& moved, const ParmVarDecl& source) {
    auto& ctx = const_cast<ASTContext&>(types_.context());
    for (const Stmt* cur = call;;) {
      auto parents = ctx.getParents(*cur);
      if (parents.empty()) break;
      const Stmt* up = parents[0].get<Stmt>();
      if (up == nullptr) break;
      if (isa<ForStmt, WhileStmt, DoStmt, CXXForRangeStmt>(up))
        return "std::move(" + moved.text + ") in a loop";
      cur = up;
    }
    const auto* fn = cast<FunctionDecl>(source.getDeclContext());
    const SourceManager& sm = ctx.getSourceManager();
    using Key = std::tuple<int, unsigned, unsigned>;  // (del, initierare, position)
    struct Use { Key key; std::optional<Path> path; const DeclRefExpr* ref; };
    std::vector<Use> uses;
    std::function<void(const Stmt*, int, unsigned)> collect = [&](const Stmt* s, int part, unsigned init) {
      if (s == nullptr) return;
      if (const auto* dr = dyn_cast<DeclRefExpr>(s); dr != nullptr && dr->getDecl() == &source) {
        const Expr* top = largestPath(dr);
        unsigned offset = sm.getFileOffset(sm.getExpansionLoc(dr->getBeginLoc()));
        uses.push_back({Key{part, init, offset}, pathOf(top), dr});
      }
      for (const Stmt* c : s->children()) collect(c, part, init);
    };
    if (const auto* ctor = dyn_cast<CXXConstructorDecl>(fn)) {
      unsigned i = 0;
      for (const CXXCtorInitializer* in : ctor->inits()) collect(in->getInit(), 0, i++);
    }
    collect(fn->getBody(), 1, 0);
    const DeclRefExpr* own = nullptr;
    std::function<void(const Stmt*)> find = [&](const Stmt* s) {
      if (s == nullptr || own != nullptr) return;
      if (const auto* dr = dyn_cast<DeclRefExpr>(s); dr != nullptr && dr->getDecl() == &source) own = dr;
      for (const Stmt* c : s->children()) find(c);
    };
    find(arg);
    auto at = std::ranges::find_if(uses, [&](const Use& u) { return u.ref == own; });
    if (own == nullptr || at == uses.end())
      return "cannot tell whether '" + source.getNameAsString() + "' is used after std::move";
    for (const Use& u : uses) {
      if (u.ref == own || !(at->key < u.key)) continue;
      if (!u.path || u.path->overlaps(moved))
        return "'" + (u.path ? u.path->text : source.getNameAsString()) + "' is used after std::move(" +
               moved.text + ")";
    }
    return std::nullopt;
  }

  // Roten i en std::move bland argumenten får inte förekomma i ett annat
  // argument: ordningen mellan argumenten är ospecificerad.
  Reason moveOrder(const CallParts& c) {
    std::vector<const Expr*> all(c.args.begin(), c.args.end());
    if (c.object != nullptr) all.push_back(c.object);
    for (const Expr* a : all) {
      const Expr* m = moveArg(a);
      if (m == nullptr) continue;
      std::optional<Path> p = pathOf(m);
      if (!p || p->root == nullptr) continue;
      bool pathMove = !p->fields.empty() && !p->open;  // STN-100
      for (const Expr* other : all)
        if (other != a && (pathMove ? mentionsOverlapping(other, *p) : mentions(other, p->root)))
          return "'" + cast<NamedDecl>(p->root)->getNameAsString() + "' is used in the same call as std::move(" + p->text + ")";
    }
    return std::nullopt;
  }

  // STN-100: förekommer en väg som överlappar moved (eller en referens in i roten)?
  bool mentionsOverlapping(const Stmt* s, const Path& moved) const {
    if (s == nullptr) return false;
    if (const auto* dr = dyn_cast<DeclRefExpr>(s)) {
      if (dr->getDecl() == moved.root) {
        std::optional<Path> q = pathOf(largestPath(dr));
        return !q || q->overlaps(moved);
      }
      if (const auto* vd = dyn_cast<VarDecl>(dr->getDecl()))
        if (auto it = refs_.find(vd); it != refs_.end() && it->second.root == moved.root) return true;
    }
    for (const Stmt* child : s->children())
      if (mentionsOverlapping(child, moved)) return true;
    return false;
  }

  bool mentions(const Stmt* s, const Decl* root) const {
    if (s == nullptr) return false;
    if (const auto* dr = dyn_cast<DeclRefExpr>(s)) {
      if (dr->getDecl() == root) return true;
      if (const auto* vd = dyn_cast<VarDecl>(dr->getDecl()))
        if (auto it = refs_.find(vd); it != refs_.end() && it->second.root == root) return true;
    }
    for (const Stmt* child : s->children())
      if (mentions(child, root)) return true;
    return false;
  }

  // En initializer_list som argument till en betrodd std-funktion är en vy
  // av en temporär backing array som finns kvar under anropet. Granska
  // elementen utan att göra initializer_list till en värdetyp som får
  // sparas eller returneras (den är varken värde eller lånevärde).
  Reason callArgument(const CallParts& c, unsigned i) {
    const Expr* argument = c.args[i];
    const Expr* init = argument->IgnoreImplicit();
    const auto* list = dyn_cast<CXXStdInitializerListExpr>(init);
    const auto* empty = dyn_cast<CXXConstructExpr>(init);
    bool emptyBraces = empty != nullptr && empty->isListInitialization() && empty->getNumArgs() == 0;
    if ((list == nullptr && !emptyBraces) || i >= c.callee->getNumParams() ||
        !inSystemHeader(*c.callee))
      return expr(argument);
    const auto* parameter = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        c.callee->getParamDecl(i)->getType()->getAsCXXRecordDecl());
    if (parameter == nullptr ||
        parameter->getSpecializedTemplate()->getQualifiedNameAsString() != "std::initializer_list")
      return expr(argument);
    if (list == nullptr && inSystemHeader(*empty->getConstructor()) &&
        types_.context().hasSameUnqualifiedType(argument->getType(), c.callee->getParamDecl(i)->getType()))
      return std::nullopt;
    if (list == nullptr) return expr(argument);
    const Expr* backing = list->getSubExpr()->IgnoreImplicit();
    const auto* array = dyn_cast<InitListExpr>(backing);
    if (array == nullptr || !array->getType()->isConstantArrayType())
      return "unsupported initializer_list backing array";
    return args(array->inits());
  }

  // Ett anrop: callee registreras, objekt och argument granskas som rena
  // uttryck, och för referensargument gäller
  //  - ett T&-argument (som inte är en temporär) är en sidoeffekt: bara
  //    tillåtet där effectHere är satt,
  //  - det måste vara en väg (variabel/fält),
  //  - det får inte överlappa ett annat referensargument eller tilldelningsmålet,
  //  - det får inte ändra något som ett levande lån pekar in i.
  // STN-69: en rå pekare är ett lån i Stone, så en ägande smartpekare får
  // aldrig ta över den (eller lämna ifrån sig sin).
  // STN-69: unique_ptr<T> från unique_ptr<U> med en annan T (en bas): T:s
  // destruktor förstör sedan objektet, vilket bara är definierat när den är
  // virtuell ([expr.delete]/3; pork_unique_ptr_review, check_base_owner).
  static Reason convertingOwner(const CXXRecordDecl& target, llvm::ArrayRef<const Expr*> args) {
    auto element = [](const CXXRecordDecl* rd) -> std::optional<QualType> {
      const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd);
      if (spec == nullptr || spec->getQualifiedNameAsString() != "std::unique_ptr" ||
          spec->getTemplateArgs().size() == 0 || spec->getTemplateArgs()[0].getKind() != TemplateArgument::Type)
        return std::nullopt;
      return spec->getTemplateArgs()[0].getAsType().getCanonicalType().getUnqualifiedType();
    };
    std::optional<QualType> to = element(&target);
    if (!to) return std::nullopt;
    for (const Expr* a : args) {
      if (a == nullptr) continue;
      std::optional<QualType> from = element(a->getType().getNonReferenceType()->getAsCXXRecordDecl());
      if (!from || *from == *to) continue;
      const CXXRecordDecl* base = (*to)->getAsCXXRecordDecl();
      const CXXDestructorDecl* dtor = base != nullptr && base->hasDefinition() ? base->getDestructor() : nullptr;
      if (dtor != nullptr && dtor->isVirtual()) continue;
      PrintingPolicy pp(target.getASTContext().getLangOpts());
      pp.SuppressTagKeyword = true;
      return "moves a std::unique_ptr<" + from->getAsString(pp) + "> into a std::unique_ptr<" + to->getAsString(pp) +
             ">, whose destructor is not virtual (deleting it is undefined)";
    }
    return std::nullopt;
  }

  // STN-90: `std::unique_ptr<T> t{o.release()}` och `t.reset(o.release())`
  // lämnar över ägandet direkt, som en flytt: samma typ (pekare och
  // deleter), och pekaren passerar ingen annanstans.
  const CXXMemberCallExpr* handedOver(const CXXMethodDecl& md, const Expr* a) const {
    const auto* rc = dyn_cast<CXXMemberCallExpr>(a->IgnoreParenImpCasts());
    if (rc == nullptr || rc->getMethodDecl() == nullptr || rc->getMethodDecl()->getNameAsString() != "release")
      return nullptr;
    const CXXRecordDecl* from = rc->getMethodDecl()->getParent();
    if (from->getQualifiedNameAsString() != "std::unique_ptr") return nullptr;
    ASTContext& ctx = md.getASTContext();
    if (!ctx.hasSameType(ctx.getCanonicalTagType(from), ctx.getCanonicalTagType(md.getParent()))) return nullptr;
    return rc;
  }

  Reason ownershipTransfer(const CallParts& c) {
    const FunctionDecl& callee = *c.callee;
    const auto* md = dyn_cast<CXXMethodDecl>(&callee);
    if (md == nullptr || !callee.getASTContext().getSourceManager().isInSystemHeader(callee.getLocation()))
      return std::nullopt;
    std::string cls = md->getParent()->getQualifiedNameAsString();
    if (cls != "std::unique_ptr" && cls != "std::shared_ptr" && cls != "std::__shared_ptr") return std::nullopt;
    if (md->getNameAsString() == "release") {
      if (c.call != nullptr && handedOver_.contains(stripNoOp(c.call))) return std::nullopt;
      return "releases ownership to a raw pointer";
    }
    if (cls == "std::unique_ptr" && (isa<CXXConstructorDecl>(md) || md->getOverloadedOperator() == OO_Equal))
      if (Reason r = convertingOwner(*md->getParent(), c.args)) return r;
    if (!isa<CXXConstructorDecl>(md) && md->getNameAsString() != "reset") return std::nullopt;
    for (const Expr* a : c.args) {
      if (a == nullptr || !a->getType()->isPointerType()) continue;
      const Expr* x = a->IgnoreParenImpCasts();
      if (const auto* d = dyn_cast<CXXDefaultArgExpr>(x)) x = d->getExpr()->IgnoreParenImpCasts();
      if (isa<CXXNullPtrLiteralExpr, GNUNullExpr, CXXScalarValueInitExpr, ImplicitValueInitExpr>(x)) continue;
      if (a->isNullPointerConstant(callee.getASTContext(), Expr::NPC_ValueDependentIsNotNull)) continue;
      if (cls == "std::unique_ptr")
        if (const CXXMemberCallExpr* rc = handedOver(*md, a)) {
          handedOver_.insert(rc);
          continue;
        }
      return "takes ownership of a raw pointer";
    }
    return std::nullopt;
  }

  // STN-70: grunt const. En const-metod i en std-typ som ger en icke-const
  // T& eller T* (unique_ptr::operator*/->/get) släpper igenom skrivningar
  // genom ett const-objekt, som pork annars låter C++ stoppa. Genom ett
  // const-objekt godtas bara läsande användning av resultatet.
  Reason shallowConst(const CallParts& c) {
    const auto* md = dyn_cast<CXXMethodDecl>(c.callee);
    if (md == nullptr || !md->isConst() || c.object == nullptr || c.call == nullptr) return std::nullopt;
    if (!md->getASTContext().getSourceManager().isInSystemHeader(md->getLocation())) return std::nullopt;
    QualType rt = md->getReturnType();
    if (!rt->isLValueReferenceType() && !rt->isPointerType()) return std::nullopt;
    if (rt->getPointeeType().isConstQualified()) return std::nullopt;
    // Objektet före clangs implicita const-konvertering för const-metoden.
    const Expr* obj = c.object->IgnoreParens();
    for (const auto* ic = dyn_cast<ImplicitCastExpr>(obj); ic != nullptr && ic->getCastKind() == CK_NoOp;
         ic = dyn_cast<ImplicitCastExpr>(obj))
      obj = ic->getSubExpr()->IgnoreParens();
    QualType ot = obj->getType();
    if (c.arrow && ot->isPointerType()) ot = ot->getPointeeType();
    if (!ot.isConstQualified()) return std::nullopt;
    if (readOnlyUse(c.call)) return std::nullopt;
    return "writable access through a const " + types_.typeName(ot.getUnqualifiedType()) +
           " (its const is shallow; only reads are allowed)";
  }

  static bool constTarget(QualType t) {
    return (t->isReferenceType() || t->isPointerType()) && t->getPointeeType().isConstQualified();
  }

  // Används värdet av e (en T& eller T*) bara för att läsa?
  bool readOnlyUse(const Expr* e) const {
    auto& ctx = const_cast<ASTContext&>(types_.context());
    const Stmt* cur = e;
    for (;;) {
      auto parents = ctx.getParents(*cur);
      if (parents.size() != 1) return false;
      if (const auto* vd = parents[0].get<VarDecl>()) return constTarget(vd->getType());
      const Stmt* up = parents[0].get<Stmt>();
      if (up == nullptr) return false;
      if (const auto* c = dyn_cast<ImplicitCastExpr>(up)) {
        switch (c->getCastKind()) {
          case CK_LValueToRValue: case CK_PointerToBoolean: return true;
          case CK_NoOp:
            if (constTarget(c->getType()) || (c->isGLValue() && c->getType().isConstQualified())) return true;
            cur = up;
            continue;
          case CK_DerivedToBase: case CK_UncheckedDerivedToBase:
            cur = up;
            continue;
          default:
            return false;
        }
      }
      if (isa<ParenExpr, ConditionalOperator, MaterializeTemporaryExpr, ExprWithCleanups>(up)) {
        cur = up;
        continue;
      }
      if (const auto* m = dyn_cast<MemberExpr>(up)) {
        if (isa<FieldDecl>(m->getMemberDecl())) {
          cur = up;
          continue;
        }
        const auto* method = dyn_cast<CXXMethodDecl>(m->getMemberDecl());
        return method != nullptr && method->isConst();
      }
      if (const auto* u = dyn_cast<UnaryOperator>(up)) {
        if (u->getOpcode() != UO_Deref && u->getOpcode() != UO_AddrOf) return false;
        cur = up;
        continue;
      }
      if (const auto* b = dyn_cast<BinaryOperator>(up)) return b->isComparisonOp();
      // Stone returnerar bara värden, const T& och const T* (STN-9/10/11).
      if (isa<ReturnStmt>(up)) return true;
      if (const auto* ce = dyn_cast<CXXConstructExpr>(up))  // kopia: T(const T&)
        return ce->getConstructor()->isCopyOrMoveConstructor() && ce->getNumArgs() == 1 &&
               ce->getConstructor()->getParamDecl(0)->getType()->getPointeeType().isConstQualified();
      if (const auto* call = dyn_cast<CallExpr>(up)) {
        const FunctionDecl* f = call->getDirectCallee();
        if (f == nullptr) return false;
        unsigned first = 0;
        if (const auto* md = dyn_cast<CXXMethodDecl>(f); md != nullptr && md->isInstance() && isa<CXXOperatorCallExpr>(call)) {
          if (call->getArg(0) == cur) return md->isConst();
          first = 1;
        }
        for (unsigned i = first; i < call->getNumArgs(); ++i) {
          if (call->getArg(i) != cur) continue;
          if (i - first >= f->getNumParams()) return false;
          QualType p = f->getParamDecl(i - first)->getType();
          return !p->isReferenceType() || p->getPointeeType().isConstQualified();
        }
        return false;
      }
      return false;
    }
  }

  Reason callWith(const CallParts& c, bool effectHere) {
    std::size_t before = handedOver_.size();
    if (auto r = ownershipTransfer(c)) return r;
    // Överlämningen är en del av den nya ägarens sidoeffekt (som en flytt).
    if (handedOver_.size() != before && effectHere)
      for (const Expr* a : c.args)
        if (const auto* md = dyn_cast<CXXMethodDecl>(c.callee))
          if (const CXXMemberCallExpr* rc = a != nullptr ? handedOver(*md, a) : nullptr) effectGranted_.insert(rc);
    if (auto r = shallowConst(c)) return r;
    if (effectHere)
      if (const CallExpr* algorithm = eraseRemove(c)) effectGranted_.insert(algorithm);
    positionReads(c, effectHere);
    const FunctionDecl& callee = *c.callee;
    if (auto r = recordCallee(callee, c.dispatch)) return r;
    if (auto r = moveOrder(c)) return r;
    std::vector<Use> captured;
    if (auto r = lambdaArguments(c, captured)) return r;
    std::vector<Use> uses = usesOf(c);
    for (const Use& u : uses) {
      if (u.mode == Mode::mutRef) markWrite(u.e);
      else markRead(u.e);
    }
    uses.insert(uses.end(), captured.begin(), captured.end());  // u.e == nullptr, bara vägen
    if (c.object != nullptr)
      if (auto r = object(c.object, c.arrow)) return r;
    for (unsigned i = 0; i < c.args.size(); ++i)
      if (!literalArg(c, i))
        if (auto r = callArgument(c, i)) return r;
    if (auto r = checkUses(uses, effectHere, "'" + displayName(callee) + "'")) return r;
    return portEffect(c, uses, effectHere);
  }

  // Ett anrop som når en port (dispatch, eller ett argument som är eller
  // innehåller en portreferens) kan nå vad som helst utanför funktionen:
  // implementationen kan hålla referenser till samma objekt som parametrarna,
  // *this eller kontexten (satta i en konstruktor), och anropa tillbaka
  // (återinträde). Det är alltid en sidoeffekt, och inget lån in i något
  // annat än en lokal variabel får leva över anropet.
  // Dispatchande anrop: referensargumenten som inte är lokala, för STN-55
  // (implementationerna kan ligga i andra TU:er, Classify avgör).
  void recordDispatch(const CallParts& c, const std::vector<Use>& uses) {
    if (dispatchCalls_ == nullptr) return;
    DispatchCall d{usrOf(*c.callee), {}};
    for (const Use& u : uses) {
      if (u.e == nullptr || u.e == c.object || u.mode == Mode::value || temporary(u)) continue;
      std::optional<Path> p = pathOfUse(u);
      if (p) {
        const auto* vd = dyn_cast_or_null<VarDecl>(p->root);
        if (vd != nullptr && !(isa<ParmVarDecl>(vd) && vd->getType()->isReferenceType()))
          continue;  // lokal variabel eller värdeparameter (objekttyper är aldrig lokala)
      }
      ArgShape a;
      a.text = p ? p->text : std::string("argument");
      a.type = TypeRules::typeKey(u.e->getType());
      types_.reachSet(u.e->getType(), a.reach);
      a.isConst = u.mode == Mode::constRef;
      if (p && p->root == nullptr && !p->open && !p->fields.empty()) {
        a.known = true;
        for (const FieldDecl* f : p->fields) {
          if (f->getType()->isReferenceType()) { a.known = false; break; }
          a.containers.insert(TypeRules::typeKey(f->getType()));
        }
      }
      d.args.push_back(std::move(a));
    }
    if (!d.args.empty()) dispatchCalls_->push_back(std::move(d));
  }

 public:
  void recordDispatchCalls(std::vector<DispatchCall>* out) { dispatchCalls_ = out; }

 private:
  Reason portEffect(const CallParts& c, const std::vector<Use>& uses, bool effectHere) {
    if (c.dispatch) recordDispatch(c, uses);
    bool reaches = c.dispatch;
    for (const Use& u : uses)
      reaches = reaches || (u.e != nullptr && !temporary(u) && types_.reachesPort(u.e->getType(), u.e == c.object));
    if (!reaches) return std::nullopt;
    std::string callee = "'" + displayName(*c.callee) + "'";
    if (!effectHere) return "side effect inside expression: call to " + callee + " reaches a port";
    return std::nullopt;  // ett lån som porten kan döda: lånemodellen (changeOutside)
  }

  // STN-74: `v.insert(v.end(), x)`. Ett v.begin()/v.end() (ev. + n) som går
  // direkt till en const_iterator-parameter som är en position eller ett
  // intervall läser bara v: den icke-const överlagringen väljs för att v inte
  // är const, men skrivrätten försvinner i konverteringen. Att iteratorn
  // pekar in i objektet kontrollerar lånemodellen.
  void positionReads(const CallParts& c, bool effectHere) {
    // STN-78: std::sort(v.begin(), v.end()) som sats: anropet, inte begin(),
    // ändrar v genom iteratorerna (lånemodellen, STN-77). En niebloid
    // (std::ranges::sort) är samma sak som en fri funktion.
    bool freeFunction = c.object == nullptr && c.callee->getOverloadedOperator() == OO_None &&
                        !isa<CXXMethodDecl>(c.callee);
    if (effectHere && inSystemHeader(*c.callee) && (freeFunction || isNiebloid(*c.callee))) {
      for (const Expr* a : c.args)
        if (const Expr* call = iteratorSource(a)) readCalls_.insert(call);
      return;
    }
    // `it != v.end()`: en jämförelse av iteratorer läser bara (lånemodellen
    // kräver att de pekar in i samma objekt).
    if (inSystemHeader(*c.callee) && isComparison(c.callee->getOverloadedOperator())) {
      std::vector<const Expr*> all(c.args.begin(), c.args.end());
      if (c.object != nullptr) all.push_back(c.object);
      for (const Expr* a : all)
        if (const Expr* call = iteratorSource(a)) readCalls_.insert(call);
      return;
    }
    if (c.object == nullptr || !inSystemHeader(*c.callee)) return;
    auto [position, range] = types_.positionArgs(*c.callee);
    if (!position && !range) return;
    for (unsigned i = 0; i < c.args.size() && i < c.callee->getNumParams(); ++i) {
      if (!(position && i == 0) && !range) continue;
      if (!isConstIterator(c.callee->getParamDecl(i)->getType())) continue;
      if (const Expr* call = iteratorSource(c.args[i])) readCalls_.insert(call);
    }
  }

  // STN-85: erase-remove-idiomet, `c.erase(ALG(c.begin(), c.end(), ...), c.end())`
  // med ALG = std::remove, std::remove_if eller std::unique, samma kända väg c
  // överallt och erase som får ha sidoeffekt. Algoritmen ändrar bara element i
  // [c.begin(), c.end()), aldrig storleken, så c.end() är samma iterator vilken
  // ordning argumenten än beräknas i. Den inre algoritmen får då samma rätt till
  // sidoeffekt som erase; lånemodellen granskar båda anropen som vanligt.
  const CallExpr* eraseRemove(const CallParts& c) const {
    if (c.object == nullptr || c.arrow || c.args.size() != 2 || !inSystemHeader(*c.callee) ||
        c.callee->getNameAsString() != "erase")
      return nullptr;
    const auto* algorithm = dyn_cast<CallExpr>(throughConversions(c.args[0]));
    if (algorithm == nullptr || isa<CXXMemberCallExpr, CXXOperatorCallExpr>(algorithm) || algorithm->getNumArgs() < 2)
      return nullptr;
    const FunctionDecl* fd = algorithm->getDirectCallee();
    if (fd == nullptr || isa<CXXMethodDecl>(fd) || !inSystemHeader(*fd)) return nullptr;
    std::string name = fd->getQualifiedNameAsString();
    if (name != "std::remove" && name != "std::remove_if" && name != "std::unique") return nullptr;
    std::optional<Path> container = pathOf(c.object);
    if (!container || container->open) return nullptr;
    bool form = containerCall(algorithm->getArg(0), {"begin", "cbegin"}, *container) &&
                containerCall(algorithm->getArg(1), {"end", "cend"}, *container) &&
                containerCall(c.args[1], {"end", "cend"}, *container);
    return form ? algorithm : nullptr;
  }

  // Genom implicita omslag och std-iteratorkonverteringar (iterator -> const_iterator).
  static const Expr* throughConversions(const Expr* e) {
    for (;;) {
      e = e->IgnoreParenImpCasts();
      if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr();
      else if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr();
      else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) e = b->getSubExpr();
      else if (const auto* ce = dyn_cast<CXXConstructExpr>(e);
               ce != nullptr && ce->getNumArgs() == 1 && inSystemHeader(*ce->getConstructor()))
        e = ce->getArg(0);
      else
        return e;
    }
  }

  // `c.begin()` (ett av namnen) direkt på vägen c, utan aritmetik.
  bool containerCall(const Expr* e, std::initializer_list<const char*> names, const Path& c) const {
    const auto* mc = dyn_cast<CXXMemberCallExpr>(throughConversions(e));
    if (mc == nullptr || mc->getNumArgs() != 0 || !inSystemHeader(*mc->getMethodDecl())) return false;
    std::string name = mc->getMethodDecl()->getNameAsString();
    if (std::none_of(names.begin(), names.end(), [&](const char* n) { return name == n; })) return false;
    const auto* member = dyn_cast<MemberExpr>(mc->getCallee()->IgnoreParens());
    if (member == nullptr || member->isArrow()) return false;
    std::optional<Path> p = pathOf(mc->getImplicitObjectArgument());
    return p && !p->open && p->root == c.root && p->fields == c.fields;
  }

  static bool isComparison(OverloadedOperatorKind k) {
    return k == OO_EqualEqual || k == OO_ExclaimEqual || k == OO_Less || k == OO_Greater || k == OO_LessEqual ||
           k == OO_GreaterEqual;
  }

  // En iterator vars operator* ger en const-referens (const_iterator).
  static bool isConstIterator(QualType t) {
    const CXXRecordDecl* rd = t.getNonReferenceType()->getAsCXXRecordDecl();
    if (rd == nullptr || !rd->hasDefinition() || !TypeRules::isGslPointerRecord(*rd)) return false;
    bool any = false;
    for (const CXXMethodDecl* m : rd->getDefinition()->methods()) {
      if (m->getOverloadedOperator() != OO_Star || m->getNumParams() != 0) continue;
      const auto* ref = m->getReturnType()->getAs<LValueReferenceType>();
      if (ref == nullptr || !ref->getPointeeType().isConstQualified()) return false;
      any = true;
    }
    return any;
  }

  // Anropet v.begin()/v.end() bakom en konvertering till const_iterator och
  // ev. `+ n`/`- n`: en std-metod som std.txt säger bara läser och som ger en iterator.
  const Expr* iteratorSource(const Expr* e) const {
    for (;;) {
      e = e->IgnoreParenImpCasts();
      if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr();
      else if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr();
      else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) e = b->getSubExpr();
      else if (const auto* ce = dyn_cast<CXXConstructExpr>(e); ce != nullptr && ce->getNumArgs() == 1 &&
                                                               inSystemHeader(*ce->getConstructor()))
        e = ce->getArg(0);
      else if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e);
               oc != nullptr && (oc->getOperator() == OO_Plus || oc->getOperator() == OO_Minus) &&
               oc->getNumArgs() == 2 && oc->getDirectCallee() != nullptr && inSystemHeader(*oc->getDirectCallee()))
        e = oc->getArg(0);
      else
        break;
    }
    // Ett fritt std-anrop med behållaren först (std::begin(v), [iterator.range]):
    // samma sak som metoden, om std.txt säger att det bara läser.
    if (const auto* fc = dyn_cast<CallExpr>(e); fc != nullptr && !isa<CXXMemberCallExpr, CXXOperatorCallExpr>(fc)) {
      const FunctionDecl* fd = fc->getDirectCallee();
      if (fd == nullptr || !inSystemHeader(*fd) || isa<CXXMethodDecl>(fd) || fc->getNumArgs() != 1) return nullptr;
      QualType frt = fd->getReturnType();
      const CXXRecordDecl* fres = frt->getAsCXXRecordDecl();
      bool fiter = frt->isPointerType() || (fres != nullptr && TypeRules::isGslPointerRecord(*fres));
      return fiter && types_.libraryReads(*fd) ? fc : nullptr;
    }
    const auto* mc = dyn_cast<CXXMemberCallExpr>(e);
    if (mc == nullptr) return nullptr;
    const CXXMethodDecl* md = mc->getMethodDecl();
    QualType rt = md->getReturnType();
    const CXXRecordDecl* result = rt->getAsCXXRecordDecl();
    // std::array::iterator är en pekare.
    bool iterator = rt->isPointerType() || (result != nullptr && TypeRules::isGslPointerRecord(*result));
    if (md->isConst() || !types_.libraryReads(*md) || !iterator) return nullptr;
    return mc;
  }

  // STN-75: initieraren av ett arrayfält i en värdetyps initieringslista
  // (std::array<int, 2>{2, 3}). Elementen och fyllnaden granskas; arrayen blir
  // aldrig ett eget värde. nullopt = inte ett sådant fält (granskas som vanligt).
  std::optional<Reason> arrayFieldInit(const InitListExpr* parent, const Expr* init) {
    if (!init->getType()->isConstantArrayType()) return std::nullopt;
    const RecordDecl* rd = parent->getType()->getAsRecordDecl();
    if (rd == nullptr || !types_.isValueType(parent->getType())) return std::nullopt;
    // Elementen ska själva vara värden: en betrodd typ får inte gömma lån
    // (pekare, iteratorer) i ett arrayfält.
    QualType element = init->getType();
    while (const ArrayType* at = element->getAsArrayTypeUnsafe()) element = at->getElementType();
    if (!types_.isValueType(element))
      return std::string("array field of ") + types_.typeName(parent->getType()) + " with elements of type " +
             types_.typeName(element);
    return arrayElements(init);
  }

  Reason arrayElements(const Expr* init) {
    init = init->IgnoreImplicit();
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(init)) return arrayElements(d->getExpr());
    if (isa<StringLiteral>(init)) return std::nullopt;
    // `Q q[2];`: varje element standardkonstrueras. En trivial konstruktor
    // lämnar elementen oinitierade.
    if (const auto* ce = dyn_cast<CXXConstructExpr>(init); ce != nullptr && ce->getType()->isConstantArrayType()) {
      if (ce->getConstructor()->isTrivial() && !ce->requiresZeroInitialization())
        return std::string("the elements of an array of ") +
               types_.typeName(ce->getType()->getAsArrayTypeUnsafe()->getElementType()) + " are not initialized";
      return constructorCall(*ce);
    }
    if (isa<ImplicitValueInitExpr>(init)) {
      // {}: elementen värdeinitieras; samma sak som `T{}` för elementtypen.
      QualType element = init->getType()->getAsArrayTypeUnsafe()->getElementType();
      if (const CXXRecordDecl* erd = element->getAsCXXRecordDecl(); erd != nullptr && erd->hasDefinition())
        for (const CXXConstructorDecl* c : erd->ctors())
          if (c->isDefaultConstructor() && !c->isTrivial())
            if (auto r = recordUserCode(*c)) return r;
      return std::nullopt;
    }
    const auto* il = dyn_cast<InitListExpr>(init);
    if (il == nullptr) return std::string("initializer of array type (") + init->getStmtClassName() + ")";
    for (const Expr* element : il->inits()) {
      if (element->getType()->isConstantArrayType()) {
        if (auto r = arrayElements(element)) return r;
      } else if (auto r = expr(element)) {
        return r;
      }
    }
    if (const Expr* filler = il->getArrayFiller(); filler != nullptr && il->hasArrayFiller()) {
      if (filler->getType()->isConstantArrayType()) {  // en rad i m[N][M] = {}
        if (auto r = arrayElements(filler)) return r;
      } else if (auto r = expr(filler)) {
        return r;
      }
    }
    return std::nullopt;
  }

  // Ett värde utan kod vid körning. En tom klass med en icke-trivial
  // destruktor är det inte: binds resultatet till en referens materialiseras
  // en temporär vid körning, och den förstörs vid körning (F4 i den
  // adversariella granskningen). Undantaget får bara stryka det som inte
  // körs (P-b i docs/stone_model.md); destruktorn granskas som vanligt.
  static bool constantValue(QualType t) {
    if (t->isArithmeticType() || t->isEnumeralType()) return true;
    const CXXRecordDecl* rd = t->getAsCXXRecordDecl();
    return rd != nullptr && rd->hasDefinition() && rd->isEmpty() && rd->hasTrivialDestructor();
  }

  // Ett const-objekt av en tom klass (niebloiden std::ranges::sort): utan
  // fält kan det inte överlappa något som skrivs.
  static bool emptyConstObject(const Use& u) {
    if (u.e == nullptr || u.mode != Mode::constRef) return false;
    const CXXRecordDecl* rd = u.e->getType()->getAsCXXRecordDecl();
    return rd != nullptr && rd->hasDefinition() && rd->isEmpty();
  }

  // En temporär, eller std::move(x) (förbrukas; movedFrom har kontrollerat x).
  bool temporary(const Use& u) const { return u.e != nullptr && (isTemporary(u.e) || moveArg(u.e) != nullptr); }
  std::optional<Path> pathOfUse(const Use& u) const { return u.path ? u.path : pathOf(u.e); }

  // Sidoeffekt- och aliasingreglerna för ett anrops referensargument.
  Reason checkUses(const std::vector<Use>& uses, bool effectHere, const std::string& callee) {
    for (const Use& u : uses) {
      if (u.mode == Mode::value || temporary(u)) continue;
      if (std::optional<Path> p = pathOfUse(u))
        if (auto r = boundUnassigned(*p)) return r;
    }
    bool modifies = false;
    for (const Use& u : uses) modifies = modifies || (u.mode == Mode::mutRef && !temporary(u));
    if (modifies && !effectHere)
      return "side effect inside expression: call to " + callee + " modifies its arguments";
    if (auto r = hiddenAlias(uses, effectHere, callee)) return r;

    for (std::size_t i = 0; i < uses.size(); ++i) {
      if (uses[i].mode != Mode::mutRef || temporary(uses[i])) continue;
      // STN-26: målet för en skrivning ska vara känt (G4). Överlapp mellan
      // argumenten (G3) avgör lånemodellen.
      std::optional<Path> p = pathOfUse(uses[i]);
      if (!p) return std::string("argument for a non-const reference must be a variable or a field");
      noteWrite(*p);
    }
    return std::nullopt;
  }

  // Kontextreferenser (`Context& game_`) är dolda referensargument: ett
  // referensargument som kan nå samma typ som en annan arguments dolda
  // referens kan vara samma objekt (utom när båda bara läser). Och en icke-const
  // dold referens kan skrivas genom, även av en const-metod: en sidoeffekt.
  Reason hiddenAlias(const std::vector<Use>& uses, bool effectHere, const std::string& callee) {
    auto text = [&](const Use& u) {
      if (std::optional<Path> p = pathOfUse(u)) return p->text;
      return std::string("argument");
    };
    for (std::size_t i = 0; i < uses.size(); ++i) {
      if (uses[i].mode == Mode::value || uses[i].e == nullptr || temporary(uses[i])) continue;
      for (const TypeRules::HiddenRef& h : types_.hiddenRefs(uses[i].e->getType())) {
        std::string ref = "reference field '" + recordName(*cast<CXXRecordDecl>(h.field->getParent())) + "::" +
                          h.field->getNameAsString() + "' of '" + text(uses[i]) + "'";
        if (!h.isConst && !effectHere)
          return "side effect inside expression: call to " + callee + " may modify through " + ref;
        for (std::size_t j = 0; j < uses.size(); ++j) {
          if (j == i || uses[j].mode == Mode::value || uses[j].e == nullptr || temporary(uses[j])) continue;
          QualType other = uses[j].e->getType();
          if (other->isPointerType()) other = other->getPointeeType();
          bool hit = types_.reaches(other, h.type);
          bool otherConst = uses[j].mode == Mode::constRef;
          for (const TypeRules::HiddenRef& g : types_.hiddenRefs(other))
            if (types_.reaches(g.type, h.type) || types_.reaches(h.type, g.type)) {
              hit = true;
              otherConst = otherConst && g.isConst;
            }
          if (hit && !(h.isConst && otherConst)) {
            // s.m(s.own): själva vägarna överlappar, det är det verkliga aliaset.
            std::optional<Path> pi = pathOfUse(uses[i]);
            std::optional<Path> pj = pathOfUse(uses[j]);
            auto direct = [](const std::optional<Path>& p) {
              return p && std::ranges::none_of(p->fields, [](const FieldDecl* f) { return f->getType()->isReferenceType(); });
            };
            if (direct(pi) && direct(pj) && pi->overlaps(*pj) &&
                (uses[i].mode == Mode::mutRef || uses[j].mode == Mode::mutRef))
              return "operands '" + pi->text + "' and '" + pj->text + "' of the call to " + callee +
                     " may be the same object and one is changed";
            return "argument '" + text(uses[j]) + "' may refer to the same object as " + ref;
          }
        }
      }
    }
    // Ett argument genom en kontextreferens (`m.game_.x`) ligger i ett okänt
    // T-objekt: det kan vara (en del av) varje annat argument som når T.
    // Två vägar genom samma referensfält jämförs som vanliga vägar.
    for (std::size_t i = 0; i < uses.size(); ++i) {
      if (uses[i].mode == Mode::value || uses[i].e == nullptr || temporary(uses[i])) continue;
      std::optional<Path> p = pathOfUse(uses[i]);
      if (!p) continue;
      for (std::size_t k = 0; k < p->fields.size(); ++k) {
        const FieldDecl* f = p->fields[k];
        if (!f->getType()->isReferenceType()) continue;
        QualType target = f->getType()->getPointeeType();
        bool refConst = target.isConstQualified();
        for (std::size_t j = 0; j < uses.size(); ++j) {
          if (j == i || uses[j].mode == Mode::value || uses[j].e == nullptr || temporary(uses[j])) continue;
          std::optional<Path> q = pathOfUse(uses[j]);
          if (q && q->root == p->root && q->fields.size() > k &&
              std::equal(p->fields.begin(), p->fields.begin() + k + 1, q->fields.begin()))
            continue;  // samma referens: vägarna avgör
          QualType other = uses[j].e->getType();
          if (other->isPointerType()) other = other->getPointeeType();
          // Ligger argumentet j i T-objektet? En väg från *this, en lokal
          // variabel eller en värdeparameter genom värdefält ligger bara i
          // objekt av typerna längs vägen; från en referensparameter även i
          // det parametern ligger i.
          bool inside = types_.reaches(target, other);
          if (q && !q->open) {
            bool known = true;
            inside = false;
            for (const FieldDecl* g : q->fields) {
              if (g->getType()->isReferenceType()) known = false;
              else if (types_.reaches(target, g->getType()) && TypeRules::typeKey(g->getType()) == TypeRules::typeKey(target))
                inside = true;
            }
            if (const auto* root = dyn_cast_or_null<ParmVarDecl>(q->root); root && root->getType()->isReferenceType())
              inside = inside || types_.reaches(target, root->getType());
            if (!known) inside = types_.reaches(target, other);
          }
          bool hit = types_.reaches(other, target) || inside;
          bool otherConst = uses[j].mode == Mode::constRef;
          for (const TypeRules::HiddenRef& g : types_.hiddenRefs(other))
            if (types_.reaches(g.type, target) || types_.reaches(target, g.type)) {
              hit = true;
              otherConst = otherConst && g.isConst;
            }
          bool mineConst = uses[i].mode == Mode::constRef || refConst;
          if (hit && !(mineConst && otherConst))
            return "arguments '" + p->text + "' (through reference field '" +
                   recordName(*cast<CXXRecordDecl>(f->getParent())) + "::" + f->getNameAsString() +
                   "') and '" + (q ? q->text : std::string("argument")) + "' may refer to the same object";
        }
      }
    }
    return std::nullopt;
  }

  // PORK_POST(R r, c): r har funktionens returtyp (ingen parameter för void),
  // c är ett rent uttryck över r, parametrarna och *this.
  Reason postReason(const FunctionDecl& fd, const LambdaExpr& le) {
    const CXXMethodDecl* op = le.getCallOperator();
    if (op->getDescribedFunctionTemplate() != nullptr) return std::string("PORK_POST result must have a type, not auto");
    QualType ret = fd.getReturnType();
    std::string want = ret->isVoidType() ? "void" : types_.typeName(ret);
    if (ret->isVoidType() ? op->getNumParams() != 0
                          : op->getNumParams() != 1 ||
                                !fd.getASTContext().hasSameUnqualifiedType(
                                    op->getParamDecl(0)->getType().getNonReferenceType(), ret))
      return "PORK_POST result must have the return type " + want;
    for (const LambdaCapture& c : le.captures())
      if (!c.capturesThis() && !(c.capturesVariable() && isa<ParmVarDecl>(c.getCapturedVar())))
        return std::string("PORK_POST can only refer to the result, the parameters and *this");
    const Expr* cond = postConditionExpr(le);
    if (cond == nullptr) return std::string("unsupported PORK_POST");
    return expr(cond);
  }

  // --- Iteratorlån ----------------------------------------------------------

  static bool inSystemHeader(const FunctionDecl& fd) {
    return fd.getASTContext().getSourceManager().isInSystemHeader(fd.getLocation());
  }

  // --- Lambdor ---------------------------------------------------------------

  // Fångster: lokala variabler av värdetyp, med referens eller som kopia.
  // Inte this, init-fångster, lokala referenser/lån eller andra lambdor.
  Reason captures(const LambdaExpr& le) {
    if (le.isMutable()) return std::string("mutable lambda (state between calls)");
    for (const LambdaCapture& c : le.captures()) {
      if (c.capturesThis()) return std::string("lambda captures 'this'");
      if (!c.capturesVariable()) return std::string("unsupported lambda capture");
      const auto* vd = dyn_cast<VarDecl>(c.getCapturedVar());
      if (vd == nullptr || vd->isInitCapture()) return std::string("lambda init capture");
      std::string name = vd->getNameAsString();
      if (unassigned_.contains(vd)) return "local variable '" + name + "' without initializer is captured by a lambda";
      if (refs_.contains(vd) || lambdas_.contains(vd))
        return "lambda captures local reference '" + name + "'";
      QualType t = vd->getType().getNonReferenceType();
      if (auto w = types_.whyNotValue(t))
        return "lambda captures '" + name + "' of type " + types_.typeName(vd->getType()) + *w;
    }
    // STN-72: fångster som kopia kopieras in i closure-objektet.
    for (const Expr* init : le.capture_inits())
      if (auto r = copiedBy(init)) return r;
    return std::nullopt;
  }

  // En kopia som inte syns som ett uttryck i koden (loopvariabel, fångst):
  // konstruktorns användarkod registreras (STN-72, STN-67).
  Reason copiedBy(const Expr* init) {
    const auto* ce = init ? dyn_cast<CXXConstructExpr>(init->IgnoreImplicit()) : nullptr;
    if (ce == nullptr || ce->getConstructor()->isTrivial()) return std::nullopt;
    return recordUserCode(*ce->getConstructor());
  }

  Reason lambdaVariable(const VarDecl& vd) {
    const Expr* init = vd.getInit() ? vd.getInit()->IgnoreImplicit() : nullptr;
    const auto* le = dyn_cast_or_null<LambdaExpr>(init);
    if (le == nullptr) {
      if (const auto* ce = dyn_cast_or_null<CXXConstructExpr>(init); ce != nullptr && ce->getNumArgs() == 1)
        if (const VarDecl* f = lambdaVar(ce->getArg(0)))
          return "lambda '" + f->getNameAsString() + "' can only be called";
      return "local variable '" + vd.getNameAsString() + "' of lambda type must be initialized with a lambda";
    }
    if (auto r = captures(*le)) return r;
    lambdas_[&vd] = le;
    return std::nullopt;
  }

  const VarDecl* lambdaVar(const Expr* e) const {
    const auto* dr = dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
    const auto* vd = dr ? dyn_cast<VarDecl>(dr->getDecl()) : nullptr;
    return vd != nullptr && lambdas_.contains(vd) ? vd : nullptr;
  }

  struct LambdaBody {
    Reason reason;
    std::set<const VarDecl*> written;  // fångade variabler som kroppen skriver till
  };

  // Kroppen granskas en gång per instans av operator(), som en egen funktion.
  const LambdaBody& lambdaBody(const CXXMethodDecl& op) {
    if (auto it = lambdaBodies_.find(&op); it != lambdaBodies_.end()) return it->second;
    LambdaBody out;
    QualType ret = op.getReturnType();
    if (!ret->isVoidType())
      if (auto w = types_.whyNotValue(ret)) out.reason = "lambda returns " + types_.typeName(ret) + *w;
    for (const ParmVarDecl* p : op.parameters())
      if (!out.reason) out.reason = types_.paramReason(*p);
    if (!out.reason) {
      Checker inner(types_, callees_, out.written);
      out.reason = inner.body(op.getBody());
    }
    return lambdaBodies_.emplace(&op, std::move(out)).first->second;
  }

  // STN-79: en lambda direkt som argument till en std-funktion. Den kopieras
  // dit och anropas där: inga referensfångster, och varje instans av
  // operator() granskas som lambdakropp.
  // std::for_each(b, e, f) returnerar f: ett resultat av closure-typ från en
  // std-funktion som fick lambdan som argument (som granskas i callWith).
  // Värdet kastas eller avvisas där det binds (lambdaVariable).
  static bool returnsLambdaArgument(const Expr* e) {
    const auto* call = dyn_cast<CallExpr>(e->IgnoreImplicit());
    const CXXRecordDecl* rd = e->getType()->getAsCXXRecordDecl();
    if (call == nullptr || rd == nullptr || !rd->isLambda() || call->getDirectCallee() == nullptr ||
        !inSystemHeader(*call->getDirectCallee()))
      return false;
    for (const Expr* a : call->arguments())
      if (a->getType()->getAsCXXRecordDecl() == rd) return true;  // lambdan själv eller en kopia av en lokal
    return false;
  }

  static const LambdaExpr* lambdaArgument(const Expr* e) {
    for (;;) {
      e = e->IgnoreParenImpCasts();
      if (const auto* w = dyn_cast<ExprWithCleanups>(e)) e = w->getSubExpr();
      else if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr();
      else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) e = b->getSubExpr();
      else if (const auto* ce = dyn_cast<CXXConstructExpr>(e); ce != nullptr && ce->getNumArgs() == 1 &&
                                                               ce->getType()->getAsCXXRecordDecl() != nullptr &&
                                                               ce->getType()->getAsCXXRecordDecl()->isLambda())
        e = ce->getArg(0);
      else
        break;
    }
    return dyn_cast<LambdaExpr>(e);
  }

  Reason lambdaArguments(const CallParts& c, std::vector<Use>& captured) {
    for (const Expr* a : c.args) {
      const LambdaExpr* le = lambdaArgument(a);
      bool variable = false;
      if (le == nullptr) {
        // En lokal lambda (lambdaVariable har granskat fångsterna), kopierad hit.
        const auto* ce = dyn_cast<CXXConstructExpr>(a->IgnoreImplicit());
        if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(a->IgnoreImplicit()))
          ce = dyn_cast<CXXConstructExpr>(m->getSubExpr()->IgnoreImplicit());
        const VarDecl* f = ce != nullptr && ce->getNumArgs() == 1 ? lambdaVar(ce->getArg(0)) : nullptr;
        if (f == nullptr) continue;
        le = lambdas_.at(f);
        variable = true;
      }
      if (!inSystemHeader(*c.callee)) return std::string("lambda as an argument of a user function");
      if (!variable)
        if (auto r = captures(*le)) return r;
      std::vector<const CXXMethodDecl*> ops;
      const CXXRecordDecl* cls = le->getLambdaClass();
      if (const FunctionTemplateDecl* t = cls->getDependentLambdaCallOperator()) {
        for (const FunctionDecl* s : t->specializations())
          if (const auto* m = dyn_cast<CXXMethodDecl>(s)) ops.push_back(m);
      } else if (const CXXMethodDecl* m = cls->getLambdaCallOperator()) {
        ops.push_back(m);
      }
      if (ops.empty()) return std::string("lambda argument without an instantiated call operator");
      std::set<const VarDecl*> written;
      for (const CXXMethodDecl* op : ops) {
        const LambdaBody& body = lambdaBody(*op);
        if (body.reason) return "in lambda argument: " + *body.reason;
        written.insert(body.written.begin(), body.written.end());
      }
      // Referensfångster får bara läsas (en skrivning syns inte i anropet);
      // de räknas som const&-argument till anropet, som i lambdaCall.
      for (const LambdaCapture& cap : le->captures()) {
        if (cap.getCaptureKind() != LCK_ByRef) continue;
        const auto* vd = llvm::cast<VarDecl>(cap.getCapturedVar());
        if (written.contains(vd))
          return "lambda argument writes '" + vd->getNameAsString() + "', captured by reference";
        captured.push_back({nullptr, Mode::constRef, Path{vd, {}, vd->getNameAsString()}});
      }
      lambdaArgs_.insert(a);
    }
    return std::nullopt;
  }

  // Ett lambdaanrop: argumenten som vanligt, fångster med referens som extra
  // argument (T& om kroppen skriver till dem, annars const T&).
  Reason lambdaCall(const CXXMethodDecl& op, const LambdaExpr& le, const std::vector<const Expr*>& args,
                    bool effectHere, const std::string& what) {
    const LambdaBody& body = lambdaBody(op);
    if (body.reason) return "in " + what + ": " + *body.reason;
    std::vector<Use> uses;
    for (unsigned i = 0; i < args.size(); ++i) uses.push_back({args[i], paramMode(op, i)});
    for (const Use& u : uses) {
      if (u.mode == Mode::mutRef) markWrite(u.e);
      else markRead(u.e);
    }
    if (auto r = this->args(args)) return r;
    for (const LambdaCapture& c : le.captures()) {
      if (c.getCaptureKind() != LCK_ByRef) continue;
      const auto* vd = llvm::cast<VarDecl>(c.getCapturedVar());
      uses.push_back({nullptr, body.written.contains(vd) ? Mode::mutRef : Mode::constRef,
                      Path{vd, {}, vd->getNameAsString()}});
    }
    return checkUses(uses, effectHere, what);
  }

  // std::visit(f, v): ett lambdaanrop per alternativ i v.
  Reason visit(const CallExpr& c, bool effectHere) {
    if (auto r = recordCallee(*c.getDirectCallee())) return r;
    if (c.getNumArgs() != 2) return std::string("std::visit with more than one variant");
    const Expr* visitor = c.getArg(0)->IgnoreImplicit();
    if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(visitor)) visitor = m->getSubExpr()->IgnoreImplicit();
    const LambdaExpr* le = dyn_cast<LambdaExpr>(visitor);
    std::string what = "lambda in std::visit";
    if (le != nullptr) {
      if (auto r = captures(*le)) return r;
    } else if (const VarDecl* f = lambdaVar(visitor)) {
      le = lambdas_.at(f);
      what = "lambda '" + f->getNameAsString() + "'";
    } else {
      return std::string("std::visit needs a lambda");
    }
    const Expr* variant = c.getArg(1);
    const auto* spec = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        variant->getType().getNonReferenceType()->getAsCXXRecordDecl());
    if (spec == nullptr || spec->getSpecializedTemplate()->getQualifiedNameAsString() != "std::variant")
      return std::string("std::visit over a non-variant");
    for (const QualType alt : variantAlternatives(*spec)) {
      const CXXMethodDecl* op = callOperatorFor(*le, alt);
      if (op == nullptr) return "no lambda call operator for " + types_.typeName(alt);
      if (auto r = lambdaCall(*op, *le, {variant}, effectHere, what)) return r;
    }
    return std::nullopt;
  }

  TypeRules& types_;
  std::vector<DispatchCall>* dispatchCalls_ = nullptr;
  std::map<std::string, Callee>& callees_;
  UserCodeScan userCode_{types_.context().getSourceManager()};  // STN-67
  const VarDecl* initializing_ = nullptr;  // variabeln vars initierare granskas just nu
  bool effectOk_ = false;                  // se expr()/effectAllowed()
  std::set<const Expr*> effectGranted_;    // STN-85: inre anrop som får ha sidoeffekt (eraseRemove)
  std::map<const VarDecl*, Path> refs_;    // lokala referenser -> vad de är bundna till
  std::map<const BindingDecl*, Path> bindingPaths_;
  std::map<const Stmt*, std::set<const VarDecl*>> liveAfter_;  // se setLiveness
  const FunctionDecl* moveFunction_ = nullptr;                   // STN-96: funktionen vars CFG söks
  std::unique_ptr<AnalysisDeclContextManager> moveCfgManager_;   // äger moveCfg_
  const CFG* moveCfg_ = nullptr;
  bool refReturn_ = false;                 // se allowRefReturn
  bool thisReturn_ = false;                // STN-73: return *this som C&
  bool memberRef_ = false;                 // STN-76: T& in i *this
  bool pointerReturn_ = false;             // se allowPointerReturn
  const FunctionDecl* analysed_ = nullptr;          // allowUnassigned
  std::set<const VarDecl*> maybeUnassigned_;        // kan läsas otilldelade (clang)
  std::set<const VarDecl*> unassigned_;             // deklarerade utan initierare
  std::set<const VarDecl*> constantStatics_;        // STN-99: statiska lokala konstanter
  std::set<const Expr*> readCalls_;        // icke-const anrop vars resultat bara läses (objectMode)
  std::map<const VarDecl*, const LambdaExpr*> lambdas_;  // lokala lambdor
  std::set<const Expr*> lambdaArgs_;       // STN-79: granskade lambdaargument
  std::map<const CXXMethodDecl*, LambdaBody> lambdaBodies_;
  std::set<const VarDecl*> ownWritten_;
  std::set<const VarDecl*>& written_ = ownWritten_;  // rötter som skrivs (för lambdakroppar)
};



}  // namespace

namespace {

class UnassignedUses : public UninitVariablesHandler {
 public:
  std::set<const VarDecl*> vars;
  void handleUseOfUninitVariable(const VarDecl* vd, const UninitUse&) override { vars.insert(vd); }
  void handleSelfInit(const VarDecl* vd) override { vars.insert(vd); }
};

// Lokala variabler i fd som kan läsas innan de tilldelats, på någon väg
// (clangs analys bakom -Wuninitialized/-Wsometimes-uninitialized). nullopt om
// CFG:n inte kunde byggas: då krävs initierare som förut.
std::optional<std::set<const VarDecl*>> maybeUnassignedLocals(const FunctionDecl& fd) {
  if (!fd.hasBody()) return std::nullopt;
  AnalysisDeclContextManager mgr(fd.getASTContext());
  mgr.getCFGBuildOptions().setAllAlwaysAdd();
  AnalysisDeclContext* ac = mgr.getContext(&fd);
  CFG* cfg = ac != nullptr ? ac->getCFG() : nullptr;
  if (cfg == nullptr) return std::nullopt;
  restoreCoveredSwitchDefaults(*cfg);
  UnassignedUses handler;
  UninitVariablesAnalysisStats stats{};
  runUninitializedVariablesAnalysis(fd, *cfg, *ac, handler, stats);
  return std::move(handler.vars);
}

// Levande variabler efter varje uttryck i fd:s CFG. killAtAssign = false: en
// tilldelning `p = x` till en referens är en användning, ingen ny bindning,
// så bara deklarationen avslutar liveness. Observer får värdet efter satsen
// (analysen går baklänges och anropas före överföringen).
std::map<const Stmt*, std::set<const VarDecl*>> liveAfterStatements(const FunctionDecl& fd) {
  std::map<const Stmt*, std::set<const VarDecl*>> out;
  if (!fd.hasBody()) return out;
  AnalysisDeclContextManager mgr(fd.getASTContext());
  mgr.getCFGBuildOptions().setAllAlwaysAdd();
  // Konstruktorns initierare i CFG:n: `data(std::move(values))` (STN-57).
  mgr.getCFGBuildOptions().AddInitializers = true;
  AnalysisDeclContext* ac = mgr.getContext(&fd);
  if (ac == nullptr || ac->getCFG() == nullptr) return out;
  restoreCoveredSwitchDefaults(*ac->getCFG());
  std::unique_ptr<LiveVariables> lv = LiveVariables::computeLiveness(*ac, false);
  if (lv == nullptr) return out;
  struct Collect : LiveVariables::Observer {
    std::map<const Stmt*, std::set<const VarDecl*>>& out;
    explicit Collect(std::map<const Stmt*, std::set<const VarDecl*>>& o) : out(o) {}
    void observeStmt(const Stmt* s, const CFGBlock*, const LiveVariables::LivenessValues& v) override {
      std::set<const VarDecl*>& live = out[s];  // ett uttryck kan förekomma flera gånger: unionen
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

}  // namespace

bool isInterfaceContract(const FunctionDecl& fd) { return hasAnnotation(fd, "pork::interface_contract"); }

const CXXMethodDecl* contractedMethod(const FunctionDecl& fd, std::string* why) {
  auto no = [&](std::string w) -> const CXXMethodDecl* {
    if (why) *why = std::move(w);
    return nullptr;
  };
  if (isa<CXXMethodDecl>(fd)) return no("an interface contract must be a free function");
  if (fd.getNumParams() == 0) return no("the first parameter of an interface contract must be the interface");
  const auto* ref = fd.getParamDecl(0)->getType()->getAs<LValueReferenceType>();
  const CXXRecordDecl* rd = ref ? ref->getPointeeType()->getAsCXXRecordDecl() : nullptr;
  if (rd == nullptr || !rd->hasDefinition() || !rd->getDefinition()->isPolymorphic())
    return no("the first parameter of an interface contract must be the interface");
  bool selfConst = ref->getPointeeType().isConstQualified();
  const ASTContext& ctx = fd.getASTContext();
  for (const CXXMethodDecl* m : rd->getDefinition()->methods()) {
    if (m->getDeclName() != fd.getDeclName() || !m->isVirtual() || m->isStatic()) continue;
    if (m->getNumParams() + 1 != fd.getNumParams() || m->isConst() != selfConst) continue;
    if (!ctx.hasSameType(m->getReturnType(), fd.getReturnType())) continue;
    bool same = true;
    for (unsigned i = 0; i < m->getNumParams() && same; ++i)
      same = ctx.hasSameType(m->getParamDecl(i)->getType(), fd.getParamDecl(i + 1)->getType());
    if (same) return m;
  }
  return no("no virtual method '" + recordName(*rd->getDefinition()) + "::" + fd.getNameAsString() +
            "' with this signature (const I& self for a const method)");
}

void classShape(const CXXMethodDecl& md, const TrustList& trust, FunctionFacts& f) {
  std::set<std::string> ignored;
  TypeRules types(md.getASTContext(), trust, ignored);
  const CXXRecordDecl* rd = md.getParent()->getDefinition();
  if (rd == nullptr) return;
  std::vector<const CXXRecordDecl*> todo{rd};
  while (!todo.empty()) {
    const CXXRecordDecl* r = todo.back();
    todo.pop_back();
    for (const CXXBaseSpecifier& b : r->bases())
      if (const CXXRecordDecl* base = b.getType()->getAsCXXRecordDecl(); base && base->hasDefinition())
        todo.push_back(base->getDefinition());
    for (const FieldDecl* fd : r->fields())
      if (!fd->getType()->isReferenceType()) types.reachSet(fd->getType(), f.fieldReach);
  }
  QualType self = md.getASTContext().getCanonicalTagType(rd);
  for (const TypeRules::HiddenRef& h : types.hiddenRefs(self)) {
    RefShape r{recordName(*cast<CXXRecordDecl>(h.field->getParent())) + "::" + h.field->getNameAsString(),
               TypeRules::typeKey(h.type), {}, h.isConst};
    types.reachSet(h.type, r.reach);
    f.contextRefs.push_back(std::move(r));
  }
  f.hasPorts = types.reachesPort(self, true);
  f.constMethod = md.isConst();
}

// P-b i docs/stone_model.md: G1 ur CFG:n. Varje element i funktionens CFG som
// kör kod (anrop, konstruktion, implicit och temporär destruktor,
// initierare) når viss användarkod (UserCodeScan, STN-67). Den ska finnas
// bland de anrop som analysen redovisat (callees). Annars har en regel
// släppt igenom kod som körs utan att den granskats: not-stone, i stället
// för ett tyst hål. Undantag (kod som inte körs eller granskas på plats):
// consteval-funktioner, pork:s kontraktsfunktioner och lambdakroppar
// (UserCodeScan följer dem inte, STN-40).
Reason cfgAccounting(const FunctionDecl& fd, const std::map<std::string, Callee>& callees) {
  if (!fd.hasBody()) return std::nullopt;
  ASTContext& ctx = fd.getASTContext();
  AnalysisDeclContextManager mgr(ctx);
  CFG::BuildOptions& opts = mgr.getCFGBuildOptions();
  opts.setAllAlwaysAdd();
  opts.AddImplicitDtors = true;
  opts.AddTemporaryDtors = true;
  opts.AddInitializers = true;
  opts.AddCXXDefaultInitExprInCtors = true;
  opts.AddCXXDefaultInitExprInAggregates = true;
  AnalysisDeclContext* ac = mgr.getContext(&fd);
  CFG* cfg = ac != nullptr ? ac->getCFG() : nullptr;
  if (cfg == nullptr) return std::string("G1: cannot build the control-flow graph");
  restoreCoveredSwitchDefaults(*cfg);
  // seen_ delas mellan elementen: en användarfunktion som setts en gång har
  // antingen godkänts (finns i callees) eller redan gett avslag, så den
  // behöver inte prövas igen.
  UserCodeScan scan(ctx.getSourceManager());
  // Kontraktsfunktionerna känns igen på annoteringen, som i resten av
  // LocalCheck (contractArgument), inte på namnrymden: egen kod i
  // namespace pork följs som all annan kod.
  auto contract = [](const FunctionDecl& f) { return isContractHelper(f); };
  auto runs = [&](const FunctionDecl* f) -> Reason {
    if (f == nullptr || f->isConsteval() || contract(*f)) return std::nullopt;
    // Redovisad själv: dess egen analys (transitivt i anropsgrafen) gäller.
    if (std::string u = usrOf(*f); callees.contains(u) || callees.contains(std::string(kDispatchPrefix) + u))
      return std::nullopt;
    for (const FunctionDecl* user : scan.from(*f)) {
      if (user->getCanonicalDecl() == fd.getCanonicalDecl()) continue;  // rekursion
      std::string u = usrOf(*user);
      if (callees.contains(u) || callees.contains(std::string(kDispatchPrefix) + u)) continue;
      return "G1: '" + displayName(*user) + "' runs here (control-flow graph) but no rule accounted for it";
    }
    return std::nullopt;
  };
  // Ett uttryck som kör kod. CFG:n går inte in i defaultargument och
  // defaultinitierare (CXXDefaultArgExpr/CXXDefaultInitExpr är löv): deras
  // uttryck följs här, med temporärernas destruktorer. Lambdakroppar ingår
  // inte i children() (de granskas där de skrivs, STN-40).
  std::function<Reason(const Stmt*, bool)> stmt = [&](const Stmt* s, bool deep) -> Reason {
    if (s == nullptr) return std::nullopt;
    const FunctionDecl* f = nullptr;
    if (const auto* c = dyn_cast<CallExpr>(s)) {
      f = c->getDirectCallee();
      if (f == nullptr && !isa<CXXPseudoDestructorExpr>(c->getCallee()->IgnoreParens()))
        return std::string("G1: call through a function pointer or member pointer (control-flow graph): "
                           "the code that runs is not known");
    } else if (const auto* c = dyn_cast<CXXConstructExpr>(s)) f = c->getConstructor();
    else if (const auto* c = dyn_cast<CXXInheritedCtorInitExpr>(s)) f = c->getConstructor();
    else if (const auto* d = dyn_cast<CXXDeleteExpr>(s)) f = d->getOperatorDelete();
    else if (const auto* n = dyn_cast<CXXNewExpr>(s)) f = n->getOperatorNew();
    else if (deep) {
      if (const auto* t = dyn_cast<CXXBindTemporaryExpr>(s)) f = t->getTemporary()->getDestructor();
    }
    if (auto r = runs(f)) return r;
    if (const auto* d = dyn_cast<CXXDefaultArgExpr>(s)) return stmt(d->getExpr(), true);
    if (const auto* d = dyn_cast<CXXDefaultInitExpr>(s)) return stmt(d->getExpr(), true);
    if (deep)
      for (const Stmt* child : s->children())
        if (auto r = stmt(child, true)) return r;
    return std::nullopt;
  };
  for (const CFGBlock* b : *cfg) {
    for (const CFGElement& el : *b) {
      Reason r;
      if (auto st = el.getAs<CFGStmt>()) r = stmt(st->getStmt(), false);
      else if (auto d = el.getAs<CFGImplicitDtor>()) r = runs(d->getDestructorDecl(ctx));
      else if (auto a = el.getAs<CFGNewAllocator>()) r = runs(a->getAllocatorExpr()->getOperatorNew());
      else if (auto c = el.getAs<CFGCleanupFunction>()) r = runs(c->getFunctionDecl());
      else if (auto i = el.getAs<CFGInitializer>()) r = stmt(i->getInitializer()->getInit(), false);
      if (r) return r;
    }
  }
  return std::nullopt;
}

Reason checkFunction(const FunctionDecl& fd, const TrustList& trust,
                     std::map<std::string, Callee>& callees,
                     std::set<std::string>& assumedTypes,
                     std::vector<DispatchCall>* dispatchCalls, const LoanSummaries* summaries) {
  // Mönstret (beroende kod) klassas inte; instanserna klassas var för sig.
  if (fd.isTemplated()) return "template pattern (classified per instantiation)";
  if (isInterfaceContract(fd)) {
    std::string why;
    if (contractedMethod(fd, &why) == nullptr) return why;
    const auto* cs = dyn_cast_or_null<CompoundStmt>(fd.getBody());
    if (cs == nullptr) return std::string("an interface contract needs a body");
    for (const Stmt* s : cs->body())
      if (preCondition(s) == nullptr && postCondition(s) == nullptr)
        return std::string("an interface contract may only contain PORK_PRE and PORK_POST");
  }
  if (fd.isVariadic()) return "variadic function";

  TypeRules types(fd.getASTContext(), trust, assumedTypes, summaries);
  if (const auto* md = dyn_cast<CXXMethodDecl>(&fd))
    if (auto r = types.methodReason(*md)) return r;

  // Returtyp: värdetyp, eller `const T&` (T värdetyp) som pekar in i *this
  // (const metod) eller en const&-parameter, se Checker::refReturnSource.
  // `const T*` (T värdetyp): en pekare som kan vara null, se pointerReturnSource.
  QualType ret = fd.getReturnType();
  bool refReturn = false, pointerReturn = false, thisReturn = false, memberRef = false;
  QualType pointee = ret->getPointeeType();  // referens eller pekare
  if (!pointee.isNull() && (ret->isLValueReferenceType() || ret->isPointerType()) && pointee.isConstQualified()) {
    if (const auto* md = dyn_cast<CXXMethodDecl>(&fd); md != nullptr && md->isInstance() && !md->isConst())
      return "return type " + types.typeName(ret) + " needs a const method";
    if (auto w = types.whyNotValue(pointee.getUnqualifiedType()))
      return "return type " + types.typeName(ret) + *w;
    (ret->isPointerType() ? pointerReturn : refReturn) = true;
  } else if (returnsThis(fd)) {
    thisReturn = true;  // STN-73: bara `return *this;`, se Checker
  } else if (returnsMemberRef(fd)) {
    if (auto w = types.whyNotValue(pointee.getUnqualifiedType()))  // STN-76
      return "return type " + types.typeName(ret) + *w;
    memberRef = true;
  } else if (!ret->isVoidType()) {
    if (auto w = types.whyNotValue(ret)) return "return type " + types.typeName(ret) + *w;
  }

  for (const ParmVarDecl* p : fd.parameters())
    if (auto r = types.paramReason(*p)) return r;

  Checker checker(types, callees);
  checker.recordDispatchCalls(dispatchCalls);
  if (refReturn) checker.allowRefReturn();
  if (pointerReturn) checker.allowPointerReturn();
  if (thisReturn) checker.allowThisReturn();
  if (memberRef) checker.allowMemberRef();
  if (auto maybe = maybeUnassignedLocals(fd)) checker.allowUnassigned(fd, std::move(*maybe));
  checker.setLiveness(fd, liveAfterStatements(fd));
  if (const auto* ctor = dyn_cast<CXXConstructorDecl>(&fd))
    if (auto r = checker.ctorInits(*ctor)) return r;
  for (const ParmVarDecl* p : fd.parameters())  // STN-68: värdeparametrar förstörs
    if (auto r = checker.destroyed(p->getType())) return r;
  if (const auto* dtor = dyn_cast<CXXDestructorDecl>(&fd))
    if (auto r = checker.destroyedMembers(*dtor->getParent())) return r;
  if (auto r = checker.body(fd.getBody(), &fd)) return r;
  return cfgAccounting(fd, callees);
}

// Alternativen i en std::variant<A, B, ...>.
std::vector<QualType> variantAlternatives(const ClassTemplateSpecializationDecl& spec) {
  std::vector<QualType> out;
  for (const TemplateArgument& a : spec.getTemplateArgs().asArray()) {
    if (a.getKind() == TemplateArgument::Pack) {
      for (const TemplateArgument& p : a.pack_elements()) out.push_back(p.getAsType());
    } else if (a.getKind() == TemplateArgument::Type) {
      out.push_back(a.getAsType());
    }
  }
  return out;
}

// operator() för ett alternativ: instansen av en generisk lambda vars
// första parameter är alternativet, annars den enda operatorn.
const CXXMethodDecl* callOperatorFor(const LambdaExpr& le, QualType alt) {
  const CXXMethodDecl* op = le.getCallOperator();
  const FunctionTemplateDecl* ft = op->getDescribedFunctionTemplate();
  if (ft == nullptr) return op;
  for (const FunctionDecl* s : ft->specializations()) {
    if (s->getNumParams() < 1) continue;
    QualType p = s->getParamDecl(0)->getType().getNonReferenceType().getUnqualifiedType();
    if (s->getASTContext().hasSameUnqualifiedType(p, alt)) return llvm::cast<CXXMethodDecl>(s);
  }
  return nullptr;
}

}  // namespace pork
