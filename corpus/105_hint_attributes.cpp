// pork-args: --silver
//
// Satsattribut som bara är tips till kompilatorn (STN-13): [[likely]],
// [[unlikely]] och [[fallthrough]] ändrar inte vad programmet gör. Andra
// attribut, som [[assume(c)]] (odefinierat om c är falskt), avvisas.

#include <pork/annotations.hpp>

// rules: STN-13
// expect: silver
int hints(int x) {
  PORK_DEPENDS_RESULT(x);
  if (x == 0) [[unlikely]] {
    return 1;
  }
  switch (x) {
    case 1: [[likely]] return 2;
    case 2:
      x = 3;
      [[fallthrough]];
    default:
      return x;
  }
}

// rules: STN-13
// expect: not-stone: unsupported statement: AttributedStmt
int assumes(int x) {
  [[assume(x > 0)]];
  return 100 / x;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"hints", [] { return hints(0) == 1 && hints(1) == 2 && hints(2) == 3 && hints(5) == 5 ? 0 : 1; }})
#endif
