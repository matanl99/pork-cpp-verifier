// pork-args: --silver
//
// Ett smalare flyttal (SLV-9): double -> float och long double -> double.
// Ett ändligt värde utanför målets intervall är odefinierat i C++
// ([conv.double]); i C blir det ±inf.

#include <pork/annotations.hpp>

// rules: SLV-9
// expect: stone: not silver: floating-point conversion overflow at 101_silver_float_narrowing.cpp:11
float bad_narrow(double d) { return static_cast<float>(d); }

// rules: SLV-9
// expect: stone: not silver: floating-point conversion overflow at 101_silver_float_narrowing.cpp:16
float bad_implicit(double d) {
  float x = d;
  return x;
}

// rules: SLV-9
// expect: stone: not silver: floating-point conversion overflow at 101_silver_float_narrowing.cpp:22
double bad_long_double(long double d) { return static_cast<double>(d); }

// rules: SLV-9
// expect: silver
float ok_bounded(double d) {
  PORK_PRE(d > -1.0e30 && d < 1.0e30);
  return static_cast<float>(d);
}

// rules: SLV-9
// expect: silver
double ok_widen(float f) { return f; }

// rules: SLV-9
// expect: silver
float ok_literal() {
  float x = 0.5;
  return x;
}
