// pork-args: --silver
//
// Typinvarianter (som SPARK Type_Invariant): PORK_INVARIANT(c) i en struct.
//  - En parameter av typen (värde, const& eller &) och *this: förvillkor.
//  - En T&-parameter, *this i en icke-const-metod och ett returvärde av
//    typen: dessutom eftervillkor.
//  - Inuti funktionen får invarianten brytas tillfälligt.
//  - Funktioner som invarianten anropar (transitivt) undantas, annars
//    blir det rekursion.

#include <pork/annotations.hpp>

struct Range {
  int lo;
  int hi;
  // rules: STN-44, SLV-19
  // expect: silver
  PORK_INVARIANT(lo <= hi && lo >= -1000 && hi <= 1000);
};

// Utan invarianten kan hi - lo svämma över.
// rules: SLV-19
// expect: silver
int width(Range r) { return r.hi - r.lo; }

// rules: SLV-19
// expect: silver
int width_ref(const Range& r) { return r.hi - r.lo; }

// rules: SLV-19
// expect: silver
void widen(Range& r) {
  if (r.hi < 1000) r.hi = r.hi + 1;
}

// rules: SLV-19
// expect: stone: not silver: invariant of 'Range' on exit from 'shift'
void shift(Range& r) { r.lo = r.lo + 1; }

// Tillfälligt brott inuti funktionen går bra.
// rules: SLV-19
// expect: silver
void swap_ends(Range& r) {
  const int lo = r.lo;
  r.lo = r.hi;
  r.hi = lo;
  r.lo = lo;
  r.hi = lo;
}

// rules: SLV-19
// expect: silver
Range make(int a) {
  PORK_PRE(a >= 0 && a <= 10);
  return {a, a + 5};
}

// rules: SLV-19
// expect: stone: not silver: invariant of 'Range' on exit from 'make_bad'
Range make_bad(int a) {
  PORK_PRE(a >= 0 && a <= 10);
  return {a + 5, a};
}

// Anroparen måste uppfylla invarianten för argumentet.
// rules: SLV-19, SLV-17
// expect: stone: not silver: invariant of 'Range' on entry to 'width'
int caller_breaks(int a) {
  PORK_PRE(a >= 0 && a <= 10);
  const Range r{a + 1, a};
  return width(r);
}

// rules: SLV-19
// expect: silver
int caller_ok(int a) {
  PORK_PRE(a >= 0 && a <= 10);
  Range r = make(a);
  widen(r);
  return width(r);
}

// Med PORK_POST: anroparen får ett resultat som uppfyller invarianten.
// rules: SLV-19, SLV-18
// expect: silver
Range grow(Range r) {
  PORK_POST(Range x, x.lo == r.lo);
  if (r.hi < 1000) r.hi = r.hi + 1;
  return r;
}

// rules: SLV-19, SLV-18
// expect: silver
int grown_width(Range r) {
  const Range g = grow(r);
  return g.hi - r.lo;
}

struct Counter {
  int n;
  // rules: STN-44, SLV-19
  // expect: silver
  PORK_INVARIANT(n >= 0 && n <= 10);

  // rules: SLV-19
  // expect: silver
  void step() {
    if (n < 10) n = n + 1;
  }
  // rules: SLV-19
  // expect: stone: not silver: invariant of 'Counter' on exit from 'Counter::reset_bad'
  void reset_bad() { n = -1; }
  // rules: SLV-19
  // expect: silver
  int scaled() const { return n * 100000000; }
};

// Invarianten anropar en hjälpmetod (och den en funktion som tar typen):
// de undantas.
struct Pair {
  int a;
  int b;
  // rules: STN-44, SLV-19
  // expect: silver
  PORK_INVARIANT(ok());
  [[nodiscard]] bool ok() const;
};

// rules: SLV-19
// expect: silver
bool in_range(const Pair& p) { return p.a >= 0 && p.a <= 1000 && p.b >= 0 && p.b <= 1000; }

// rules: STN-44, SLV-19
// expect: silver
bool Pair::ok() const { return in_range(*this); }

// rules: SLV-19
// expect: silver
int pair_sum(const Pair& p) { return (p.a + p.b) * 1000000; }

// En invariant som inte är Stone kan inte översättas, och då inte heller det
// som använder typen. (Kraschade tidigare: closure() avrefererade en saknad
// översättning.)
int invariant_limit = 10;
struct Unchecked {
  int n;
  // rules: STN-44
  // expect: not-stone: uses variable 'invariant_limit'
  PORK_INVARIANT(n < invariant_limit);
};

// rules: STN-44, SLV-19
// expect: stone: not silver: not translated to C 'Unchecked::pork_invariant'
int use_unchecked(Unchecked u) { return u.n; }
