#pragma once

// std::vector<bool>: icke-const operator[]/at/front/back returnerar en proxy
// (libstdc++: std::_Bit_reference) i stället för bool&. pork behandlar proxyn
// som själva elementet, men bara i två former:
//   - läsning: proxyn konverteras till bool (operator bool),
//   - skrivning: `v[i] = <bool>` (proxyns operator=(bool)).
// Allt annat med proxyn (spara den, flip(), skicka den vidare) känns inte igen
// och ger not-stone som andra klasstyper.

#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

#include <utility>

namespace pork {

inline bool isVectorBool(const clang::CXXRecordDecl* rd) {
  const auto* spec = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(rd);
  if (spec == nullptr || spec->getQualifiedNameAsString() != "std::vector") return false;
  const clang::TemplateArgumentList& args = spec->getTemplateArgs();
  return args.size() >= 1 && args[0].getKind() == clang::TemplateArgument::Type &&
         args[0].getAsType()->isBooleanType();
}

// En accessor på std::vector<bool> som ger proxyn (inte bool&)?
inline bool isVectorBoolProxyAccessor(const clang::CXXMethodDecl& md) {
  if (md.isConst() || !isVectorBool(md.getParent())) return false;
  std::string n = md.getNameAsString();
  if (n != "operator[]" && n != "at" && n != "front" && n != "back") return false;
  clang::QualType rt = md.getReturnType();
  return !rt->isReferenceType() && rt->getAsCXXRecordDecl() != nullptr;
}

// Accessor-anropet (v[i] m.fl.) som ger proxyn, genom implicita NoOp-casts och
// temporärer. Annars nullptr.
inline const clang::Expr* bitElement(const clang::Expr* e) {
  using namespace clang;
  while (true) {
    e = e->IgnoreParens();
    if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) {
      e = m->getSubExpr();
    } else if (const auto* b = dyn_cast<CXXBindTemporaryExpr>(e)) {
      e = b->getSubExpr();
    } else if (const auto* c = dyn_cast<ImplicitCastExpr>(e); c != nullptr && c->getCastKind() == CK_NoOp) {
      e = c->getSubExpr();
    } else {
      break;
    }
  }
  const CXXMethodDecl* md = nullptr;
  if (const auto* oc = dyn_cast<CXXOperatorCallExpr>(e))
    md = dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
  else if (const auto* mc = dyn_cast<CXXMemberCallExpr>(e))
    md = mc->getMethodDecl();
  if (md == nullptr || !isVectorBoolProxyAccessor(*md)) return nullptr;
  return e;
}

// Läsning: `operator bool()` på proxyn (ev. inuti en implicit konvertering).
// Elementanropet, annars nullptr.
inline const clang::Expr* bitRead(const clang::Expr* e) {
  using namespace clang;
  e = e->IgnoreParens();
  if (const auto* c = dyn_cast<ImplicitCastExpr>(e); c != nullptr && c->getCastKind() == CK_UserDefinedConversion)
    e = c->getSubExpr()->IgnoreParens();
  const auto* mc = dyn_cast<CXXMemberCallExpr>(e);
  if (mc == nullptr || !isa_and_nonnull<CXXConversionDecl>(mc->getMethodDecl()) || !mc->getType()->isBooleanType())
    return nullptr;
  return bitElement(mc->getImplicitObjectArgument());
}

// Skrivning `v[i] = x` med x av typen bool: (elementanropet, x), annars {nullptr, nullptr}.
inline std::pair<const clang::Expr*, const clang::Expr*> bitWrite(const clang::Expr* e) {
  using namespace clang;
  const auto* oc = dyn_cast<CXXOperatorCallExpr>(e->IgnoreParens());
  if (oc == nullptr || oc->getOperator() != OO_Equal || oc->getNumArgs() != 2) return {nullptr, nullptr};
  if (!oc->getArg(1)->getType()->isBooleanType()) return {nullptr, nullptr};
  const Expr* el = bitElement(oc->getArg(0));
  if (el == nullptr) return {nullptr, nullptr};
  return {el, oc->getArg(1)};
}

}  // namespace pork
