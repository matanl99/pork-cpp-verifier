// pork-args: --silver
//
// Konstruktioner som bara finns vid kompileringen. Hittade när yaml-cpp och
// fmt mättes: static_assert i en funktionskropp (STN-64) och
// mallparametrar som inte är typer (STN-65).

#include <pork/annotations.hpp>

// --- static_assert (STN-64) ---

// rules: STN-64
// expect: silver
int halve(int x) {
  static_assert(sizeof(int) >= 2, "int is at least 16 bits");
  return x / 2;
}

// Även bland vanliga deklarationer och i en mall.
// rules: STN-64
// expect: silver
template <typename T> T first_or(T a, T fallback) {
  static_assert(sizeof(T) <= 8);
  T r = a;
  if (r == 0) r = fallback;
  return r;
}
template int first_or<int>(int, int);

// --- mallparametrar som inte är typer (STN-65) ---

// rules: STN-65
// expect: flag<true>: silver
// expect: flag<false>: silver
template <bool B> int flag() { return B ? 1 : 0; }

// rules: STN-65
// expect: silver
int flags() { return flag<true>() + flag<false>(); }

// rules: STN-65
// expect: silver
template <int N> int scale(int x) {
  PORK_PRE(x > -1000 && x < 1000);
  return x * N;
}
template int scale<3>(int);

// Mallparametern som loopgräns.
// rules: STN-65
// expect: silver
template <unsigned N> unsigned sum_to() {
  unsigned s = 0;
  for (unsigned i = 0; i < N; ++i) s += i;
  return s;
}

// rules: STN-65
// expect: silver
unsigned ten() { return sum_to<5>(); }

// Det insatta värdet räknas: x * 10000000 svämmar över för x nära 1000.
// rules: STN-65
// expect: stone: not silver
template <int N> int scale_big(int x) {
  PORK_PRE(x >= 0 && x < 1000);
  return x * N;
}
template int scale_big<10000000>(int);
