// pork-args: --silver --silver-strict
//
// Strikt Silver (--silver-strict), som SPARK: utöver odefinierat beteende
// underkänns även omslag i unsigned-aritmetik och heltalskonverteringar där
// värdet inte ryms i måltypen (implicita och explicita). Båda är definierade
// i C++ men ger fel värde (CWE-190/CWE-197). Se corpus/20_silver.cpp för
// Silver utan --silver-strict.

#include <cstdint>
#include <pork/annotations.hpp>

// --- unsigned-omslag ---

// rules: SLV-29, SLV-31
// expect: stone: not silver: arithmetic overflow on unsigned +
// expect[esbmc]: stone: not silver: arithmetic overflow on integer +
unsigned wrap_add(unsigned x) { return x + 1U; }

// rules: SLV-29, SLV-17
// expect: silver
unsigned bounded_add(unsigned x) {
  PORK_PRE(x < 1000U);
  return x + 1U;
}

// rules: SLV-29, SLV-31
// expect: stone: not silver: arithmetic overflow on unsigned -
// expect[esbmc]: stone: not silver: arithmetic overflow on integer -
unsigned wrap_sub(unsigned a, unsigned b) { return a - b; }

// rules: SLV-29
// expect: silver
unsigned guarded_sub(unsigned a, unsigned b) { return a >= b ? a - b : 0U; }

// --- implicit konvertering tillbaka till en smalare typ ---

// `c + 1` räknas i int; konverteringen tillbaka till char kan tappa värdet.
// rules: SLV-30
// expect: stone: not silver: integer conversion out of range
char char_inc(char c) {
  char r = c + 1;
  return r;
}

// rules: SLV-30, SLV-17
// expect: silver
char char_inc_bounded(char c) {
  PORK_PRE(c < 100);
  char r = c + 1;
  return r;
}

// Bredare mål: alla värden ryms, ingen kontroll behövs.
// rules: SLV-30
// expect: silver
long widen(int x) { return x; }

// --- explicit konvertering ---

// rules: SLV-30
// expect: stone: not silver: integer conversion out of range
std::int16_t narrow(int x) { return static_cast<std::int16_t>(x); }

// rules: SLV-30, SLV-17
// expect: silver
std::int16_t narrow_bounded(int x) {
  PORK_PRE(x >= -32768 && x <= 32767);
  return static_cast<std::int16_t>(x);
}

// --- tecken ---

// Negativt int till unsigned.
// rules: SLV-30
// expect: stone: not silver: integer conversion out of range
unsigned to_unsigned(int x) { return static_cast<unsigned>(x); }

// rules: SLV-30
// expect: silver
unsigned to_unsigned_checked(int x) { return x < 0 ? 0U : static_cast<unsigned>(x); }

// Stora unsigned till int.
// rules: SLV-30
// expect: stone: not silver: integer conversion out of range
int to_signed(unsigned x) { return static_cast<int>(x); }

// En heltalskonstant som ryms i måltypen kontrolleras inte (1 blir unsigned long).
// rules: SLV-30
// expect: silver
unsigned long minus_one(unsigned long x) { PORK_PRE(x > 0UL); return x - 1; }

// En som inte ryms gör det.
// rules: SLV-30
// expect: stone: not silver: integer conversion out of range
unsigned all_ones() { return static_cast<unsigned>(-1); }
