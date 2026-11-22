#pragma once

// Dispatchande anrop: ett virtuellt anrop som inte går att avgöra statiskt
// (inte kvalificerat som Base::f(), inte på en final klass eller metod). Det
// kan nå vilken override som helst. Delas av LocalCheck (Stone: alla kända
// implementationer måste hålla, okända är ett antagande) och CTranslate
// (Silver: kräver gränssnittets kontrakt, finns inte än).

#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

namespace pork {

// `call` är anropet, `md` den anropade metoden och `object` objektuttrycket.
inline bool isDispatch(const clang::CallExpr& call, const clang::CXXMethodDecl& md, const clang::Expr* object) {
  using namespace clang;
  if (!md.isVirtual() || object == nullptr) return false;
  if (const auto* mc = dyn_cast<CXXMemberCallExpr>(&call))
    if (const auto* me = dyn_cast<MemberExpr>(mc->getCallee()->IgnoreParens()); me != nullptr && me->hasQualifier())
      return false;
  return md.getDevirtualizedMethod(object, /*IsAppleKext=*/false) == nullptr;
}

}  // namespace pork
