#pragma once

// `assert(c)` från <assert.h>/<cassert> som egen sats. Delas av LocalCheck
// (Stone: c är ett rent uttryck, resten av makrot ignoreras) och CTranslate
// (Silver: __CPROVER_assert(c, "assertion 'c'"), som SPARK:s pragma Assert).
//
// Makrot expanderar olika i olika libc och språk, t.ex. glibc:
//   C++: (static_cast<bool>(c) ? void(0) : __assert_fail("c", __FILE__, ...))
//   C:   ((void)sizeof((c) ? 1 : 0), __extension__ ({ if (c) ; else __assert_fail("c", ...); }))
// Igenkänningen är därför: satsen börjar i en expansion av makrot `assert`,
// innehåller ett anrop till libc:s felfunktion med villkoret som strängliteral,
// och villkoret är det första största deluttrycket som kommer från makrots
// argument (utanför sizeof). Med NDEBUG är assert `((void)0)` och inget av
// detta gäller.

#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>

#include <optional>
#include <string>

namespace pork {

struct AssertMacro {
  const clang::Expr* condition;  // c, som det står i källan
  std::string text;              // c som text (#c i makrot)
};

namespace detail {

// libc:s felfunktion och vilket argument som är #c: glibc/musl/bionic först,
// macOS sist.
inline std::optional<std::string> assertFailText(const clang::CallExpr& call) {
  using namespace clang;
  const FunctionDecl* fn = call.getDirectCallee();
  if (fn == nullptr || fn->getIdentifier() == nullptr || call.getNumArgs() == 0) return std::nullopt;
  llvm::StringRef name = fn->getName();
  unsigned index;
  if (name == "__assert_fail" || name == "__assert" || name == "__assert2")
    index = name == "__assert2" ? 3 : 0;
  else if (name == "__assert_rtn")
    index = call.getNumArgs() - 1;
  else
    return std::nullopt;
  if (index >= call.getNumArgs()) return std::nullopt;
  if (const auto* s = dyn_cast<StringLiteral>(call.getArg(index)->IgnoreParenImpCasts()))
    return s->getString().str();
  return std::nullopt;
}

inline const clang::CallExpr* findAssertFail(const clang::Stmt* s) {
  if (s == nullptr) return nullptr;
  if (const auto* c = llvm::dyn_cast<clang::CallExpr>(s); c != nullptr && assertFailText(*c)) return c;
  for (const clang::Stmt* child : s->children())
    if (const clang::CallExpr* c = findAssertFail(child)) return c;
  return nullptr;
}

inline const clang::Expr* firstArgumentExpr(const clang::Stmt* s, const clang::SourceManager& sm,
                                            const clang::CallExpr* fail) {
  using namespace clang;
  if (s == nullptr || s == fail || isa<UnaryExprOrTypeTraitExpr>(s)) return nullptr;
  if (const auto* e = dyn_cast<Expr>(s); e != nullptr && e->getBeginLoc().isMacroID() &&
                                          sm.isMacroArgExpansion(e->getBeginLoc()))
    return e;
  for (const Stmt* child : s->children())
    if (const Expr* e = firstArgumentExpr(child, sm, fail)) return e;
  return nullptr;
}

}  // namespace detail

// assert(...) med NDEBUG: ((void)0) från makrot assert (också genom ett
// hjälpmakro som glibc:s __ASSERT_VOID_CAST). Inget körs; i en kontrakts-
// inledning står den på samma plats som den påslagna assert:en.
inline bool disabledAssert(const clang::Stmt* s, const clang::SourceManager& sm, const clang::LangOptions& lang) {
  using namespace clang;
  const auto* e = dyn_cast_or_null<Expr>(s);
  if (e == nullptr || !e->getBeginLoc().isMacroID()) return false;
  const auto* cast = dyn_cast<ExplicitCastExpr>(e->IgnoreParens());
  if (cast == nullptr || !cast->getType()->isVoidType()) return false;
  const auto* zero = dyn_cast<IntegerLiteral>(cast->getSubExpr()->IgnoreParenImpCasts());
  if (zero == nullptr || zero->getValue() != 0) return false;
  for (SourceLocation loc = e->getBeginLoc(); loc.isMacroID(); loc = sm.getImmediateMacroCallerLoc(loc))
    if (Lexer::getImmediateMacroName(loc, sm, lang) == "assert") return true;
  return false;
}

// Hela satsen är en expansion av assert(...), påslagen eller avstängd. För
// placeringen i ett mallmönster, där assert:ens anrop ännu är en olöst
// uppslagning (assertMacro känner igen den först i instansen).
inline bool wholeAssertExpansion(const clang::Stmt* s, const clang::SourceManager& sm,
                                 const clang::LangOptions& lang) {
  using namespace clang;
  if (s == nullptr) return false;
  SourceLocation b = s->getBeginLoc(), e = s->getEndLoc();
  if (!b.isMacroID() || !e.isMacroID() || sm.getExpansionLoc(b) != sm.getExpansionLoc(e)) return false;
  for (SourceLocation loc = b; loc.isMacroID(); loc = sm.getImmediateMacroCallerLoc(loc))
    if (Lexer::getImmediateMacroName(loc, sm, lang) == "assert") return true;
  return false;
}

inline std::optional<AssertMacro> assertMacro(const clang::Stmt* s, const clang::SourceManager& sm,
                                              const clang::LangOptions& lang) {
  using namespace clang;
  const auto* e = dyn_cast_or_null<Expr>(s);
  if (e == nullptr || !e->getBeginLoc().isMacroID()) return std::nullopt;
  if (Lexer::getImmediateMacroName(e->getBeginLoc(), sm, lang) != "assert") return std::nullopt;
  const CallExpr* fail = detail::findAssertFail(e);
  if (fail == nullptr) return std::nullopt;
  const Expr* cond = detail::firstArgumentExpr(e, sm, fail);
  if (cond == nullptr) return std::nullopt;
  return AssertMacro{cond, *detail::assertFailText(*fail)};
}

}  // namespace pork
