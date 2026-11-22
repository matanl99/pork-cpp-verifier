#pragma once

// std::move(x): ett lån av x som förbrukas. Delas av LocalCheck (Stone: x
// ska vara en lokal variabel eller värdeparameter som inte används efteråt,
// STN-57) och CTranslate (Silver: en kopia av x; x används aldrig igen).

#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>

namespace pork {

// Argumentet om `e` är ett anrop av std::move med ett argument, annars nullptr.
inline const clang::Expr* moveArg(const clang::Expr* e) {
  using namespace clang;
  if (e == nullptr) return nullptr;
  e = e->IgnoreParenImpCasts();
  if (const auto* m = dyn_cast<MaterializeTemporaryExpr>(e)) e = m->getSubExpr()->IgnoreParenImpCasts();
  const auto* c = dyn_cast<CallExpr>(e);
  if (c == nullptr || c->getNumArgs() != 1 || isa<CXXMemberCallExpr, CXXOperatorCallExpr>(c)) return nullptr;
  const FunctionDecl* fd = c->getDirectCallee();
  if (fd == nullptr || !fd->isInStdNamespace() || fd->getIdentifier() == nullptr || fd->getName() != "move")
    return nullptr;
  return c->getArg(0);
}

}  // namespace pork
