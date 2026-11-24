#pragma once

// Satsattribut som inte ändrar vad programmet gör: [[likely]], [[unlikely]]
// och [[fallthrough]]. Delas av LocalCheck, CTranslate, FlowBuild och LoanCheck,
// som ser igenom dem. Andra attribut ([[assume(x)]], musttail) granskas inte
// här och avvisas av respektive tillåtelselista.

#include <clang/AST/Attr.h>
#include <clang/AST/Stmt.h>

namespace pork {

inline bool isHintOnly(const clang::AttributedStmt& a) {
  for (const clang::Attr* attr : a.getAttrs())
    if (!clang::isa<clang::LikelyAttr, clang::UnlikelyAttr, clang::FallThroughAttr>(attr)) return false;
  return true;
}

// Satsen under sådana attribut.
inline const clang::Stmt* withoutHints(const clang::Stmt* s) {
  while (const auto* a = clang::dyn_cast_or_null<clang::AttributedStmt>(s)) {
    if (!isHintOnly(*a)) break;
    s = a->getSubStmt();
  }
  return s;
}

}  // namespace pork
