#pragma once

// En switch över en enum som har ett fall för varje uppräknat värde och inget
// default: clangs CFG markerar vägen förbi alla fall som oåtkomlig. I C++ kan
// en enum ha andra värden (`static_cast<E>(7)` för en enum med fast typ, och
// värden inom intervallet för en enum utan), och då tar switchen ingen gren.
// Analyserna (oinitierade lokaler, liveness, lån, flöde, G1) ska se den vägen,
// så alla bygger sin CFG med restoreCoveredSwitchDefaults.

#include <clang/AST/Stmt.h>
#include <clang/Analysis/CFG.h>

namespace pork {

// Gör de oåtkomliga efterföljarna till en sådan switch åtkomliga igen, i båda
// riktningarna (efterföljare och föregångare).
inline void restoreCoveredSwitchDefaults(clang::CFG& cfg) {
  using namespace clang;
  for (CFGBlock* b : cfg) {
    const auto* s = dyn_cast_or_null<SwitchStmt>(b->getTerminatorStmt());
    if (s == nullptr || !s->isAllEnumCasesCovered()) continue;
    for (auto it = b->succ_begin(); it != b->succ_end(); ++it) {
      CFGBlock* target = it->getPossiblyUnreachableBlock();
      if (it->isReachable() || target == nullptr) continue;
      *it = CFGBlock::AdjacentBlock(target, true);
      for (auto p = target->pred_begin(); p != target->pred_end(); ++p)
        if (!p->isReachable() && p->getPossiblyUnreachableBlock() == b) *p = CFGBlock::AdjacentBlock(b, true);
    }
  }
}

}  // namespace pork
