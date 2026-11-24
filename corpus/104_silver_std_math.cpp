// pork-args: --silver
//
// C-funktioner som <cmath>/<cstdlib> exporterar i std med en using-deklaration
// (`namespace std { using ::sqrt; }`): std::sqrt(double) är glibc:s ::sqrt.
// Namnet är std::sqrt, trust-filen betror det (och godtar även "sqrt"), och
// Silver använder kontraktet std__sqrt__double i contracts/std.c (STN-24).

#include <cmath>
#include <cstdlib>
#include <pork/annotations.hpp>

// rules: STN-24, SLV-21
// expect: silver: assuming std::sqrt
double root(double x) {
  PORK_PRE(x >= 0.0 && x < 1.0e6);
  return std::sqrt(x);
}

// rules: STN-24, SLV-21
// expect: stone: not silver: precondition of 'std::sqrt': x >= 0
double bad_root(double x) {
  PORK_PRE(x < 1.0e6);
  return std::sqrt(x);
}

// rules: STN-24, SLV-21
// expect: stone: not silver: precondition of 'std::abs': x != INT_MIN
int bad_abs(int x) { return std::abs(x); }

// rules: STN-24, SLV-21
// expect: silver: assuming std::abs
int ok_abs(int x) {
  PORK_PRE(x > -1000);
  return std::abs(x);
}

// rules: STN-24, SLV-21
// expect: silver: assuming std::fabs
double magnitude(double x) { return std::fabs(x); }
