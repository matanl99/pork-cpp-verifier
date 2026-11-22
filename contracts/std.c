// Kontrakt för betrodda standardfunktioner i Silver (CBMC).
//
// Namn: det kvalificerade namnet med :: -> __, följt av parametertyperna utan
// const/& (mellanslag -> _): std::hypot(float, float) -> std__hypot__float__float.
//
// Ett kontrakt säger vad anroparen får anta om resultatet (__CPROVER_assume)
// och vad anroparen måste uppfylla (__CPROVER_assert med "precondition of").
// Det är antaganden, inte bevis: pork redovisar dem som "assuming".
// Kontrakten granskas själva av CBMC, så undvik aritmetik som kan svämma över.

#include "pork_contracts.h"

// --- Absolutbelopp ---------------------------------------------------------

float std__fabs__float(float x) { return x < 0.0f ? -x : x; }
double std__fabs__double(double x) { return x < 0.0 ? -x : x; }
float std__abs__float(float x) { return x < 0.0f ? -x : x; }
double std__abs__double(double x) { return x < 0.0 ? -x : x; }

int std__abs__int(int x) {
  __CPROVER_assert(x != -2147483647 - 1, "precondition of 'std::abs': x != INT_MIN");
  return x < 0 ? -x : x;
}

// --- hypot -----------------------------------------------------------------
// max(|x|, |y|) <= r <= |x| + |y|; r = 0 bara för (0, 0). Om |x| + |y| inte
// ryms kan resultatet svämma över: det räknas som ett fel, som för +.

float std__hypot__float__float(float x, float y) {
  double ax = x < 0.0f ? -(double)x : (double)x;
  double ay = y < 0.0f ? -(double)y : (double)y;
  __CPROVER_assert(ax + ay <= 3.4028234663852886e38, "std::hypot overflows");
  float r = nondet_float();
  __CPROVER_assume(PORK_FINITE_F(r));
  __CPROVER_assume((double)r >= ax && (double)r >= ay && (double)r <= ax + ay);
  __CPROVER_assume(ax == 0.0 && ay == 0.0 ? r == 0.0f : r > 0.0f);
  return r;
}

double std__hypot__double__double(double x, double y) {
  long double ax = x < 0.0 ? -(long double)x : (long double)x;
  long double ay = y < 0.0 ? -(long double)y : (long double)y;
  __CPROVER_assert(ax + ay <= 1.7976931348623157e308L, "std::hypot overflows");
  double r = nondet_double();
  __CPROVER_assume(PORK_FINITE_D(r));
  __CPROVER_assume((long double)r >= ax && (long double)r >= ay && (long double)r <= ax + ay);
  __CPROVER_assume(ax == 0.0L && ay == 0.0L ? r == 0.0 : r > 0.0);
  return r;
}

// --- sqrt ------------------------------------------------------------------

float std__sqrt__float(float x) {
  __CPROVER_assert(x >= 0.0f, "precondition of 'std::sqrt': x >= 0");
  float r = nondet_float();
  __CPROVER_assume(PORK_FINITE_F(r) && r >= 0.0f);
  __CPROVER_assume(x == 0.0f ? r == 0.0f : r > 0.0f);
  __CPROVER_assume(x >= 1.0f ? r <= x : (r >= x && r <= 1.0f));
  return r;
}

double std__sqrt__double(double x) {
  __CPROVER_assert(x >= 0.0, "precondition of 'std::sqrt': x >= 0");
  double r = nondet_double();
  __CPROVER_assume(PORK_FINITE_D(r) && r >= 0.0);
  __CPROVER_assume(x == 0.0 ? r == 0.0 : r > 0.0);
  __CPROVER_assume(x >= 1.0 ? r <= x : (r >= x && r <= 1.0));
  return r;
}

// --- ceil ------------------------------------------------------------------
// Ett heltal r >= x med r - 1 < x. Från 2^23 (float) / 2^52 (double) är alla
// flyttal heltal och ceil(x) == x; där är r - 1 == r, så r == x krävs.

float std__ceil__float(float x) {
  float r = nondet_float();
  __CPROVER_assume(PORK_FINITE_F(r) && r >= x && (r == x || r - 1.0f < x));
  return r;
}

double std__ceil__double(double x) {
  double r = nondet_double();
  __CPROVER_assume(PORK_FINITE_D(r) && r >= x && (r == x || r - 1.0 < x));
  return r;
}

// --- clamp, min, max (exakta) ------------------------------------------------

#define PORK_CLAMP(T, N)                                                          \
  T std__clamp__##N##__##N##__##N(T v, T lo, T hi) {                              \
    __CPROVER_assert(!(hi < lo), "precondition of 'std::clamp': !(hi < lo)");     \
    return v < lo ? lo : hi < v ? hi : v;                                         \
  }                                                                               \
  T std__min__##N##__##N(T a, T b) { return b < a ? b : a; }                      \
  T std__max__##N##__##N(T a, T b) { return a < b ? b : a; }

PORK_CLAMP(float, float)
PORK_CLAMP(double, double)
PORK_CLAMP(int, int)
PORK_CLAMP(long, long)
PORK_CLAMP(unsigned long, unsigned_long)
