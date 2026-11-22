// SLV-43: ett kast är ett körtidsfel i Silver. Beviset ska visa att satsen
// inte nås under förvillkoren; då försvinner antagandet 'throws E'.
// pork-args: --silver
#include <pork/annotations.hpp>
#include <stdexcept>

// rules: SLV-43
// expect: stone: not silver: throw std::length_error is reachable
int reachable(int x) {
  if (x < 0) throw std::length_error("negative");
  return x;
}

// rules: SLV-43
// expect: silver
int unreachable(int x) {
  PORK_PRE(x >= 0 && x < 100);
  if (x < 0) throw std::length_error("negative");
  return x + 1;
}

// rules: SLV-43
// expect: silver
int caller_ok(int x) {
  PORK_PRE(x >= 0 && x < 100);
  return unreachable(x);
}

// rules: SLV-43
// expect: stone: not silver: throw std::length_error is reachable
int caller_bad(int x) { return reachable(x); }
