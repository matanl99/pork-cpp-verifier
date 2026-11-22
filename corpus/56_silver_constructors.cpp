// pork-args: --silver
//
// Konstruktorer i Silver (SLV-39). En konstruktor blir
// `void C(struct T *self, ...)`: initieringslistan som `self->f = ...;` i
// ordning, sedan kroppen. Anropet blir ett satsuttryck med en temporär.
// Objektet antas inte giltigt vid start; en typinvariant ska gälla efteråt.

#include <pork/annotations.hpp>

struct Vec {
  float x;
  float y;
  // rules: SLV-39
  // expect: silver
  Vec(float a, float b) : x(a), y(b) {}
};

// rules: SLV-39
// expect: silver
Vec diagonal(float a) { return Vec(a, a); }

// Värdet kommer fram till anroparen.
// rules: SLV-39
// expect: silver
int built() {
  PORK_POST(int r, r == 1);
  Vec v(1.0F, 2.0F);
  return v.x < v.y ? 1 : 0;
}

// Överspill i en initierare.
struct Product {
  int value;
  // rules: SLV-39
  // expect: stone: not silver: arithmetic overflow on signed *
  Product(int a, int k) : value(a * k) {}
};

// Med förvillkor: kontrolleras hos anroparen.
struct Bounded {
  int value;
  // rules: SLV-39
  // expect: silver
  explicit Bounded(int a) : value(a * 2) { PORK_PRE(a > -1000 && a < 1000); }
};

// rules: SLV-39
// expect: silver
Bounded ok() { return Bounded(10); }

// rules: SLV-39
// expect: stone: not silver: precondition of 'Bounded::Bounded'
Bounded too_big() { return Bounded(5000); }

// Kroppen efter initieringslistan.
struct Late {
  int x;
  // rules: SLV-39
  // expect: stone: not silver: arithmetic overflow on signed +
  explicit Late(int a) : x(0) { x = a + 1; }
};

// Standardinitierare och fält med egen konstruktor.
struct Line {
  Vec from;
  Vec to;
  int width = 1;
  // rules: SLV-39
  // expect: silver
  Line(float a, float b) : from(a, b), to(b, a) {}
};

// Delegering.
struct Pair {
  int a;
  int b;
  // rules: SLV-39
  // expect: silver
  Pair(int x, int y) : a(x), b(y) {}
  // rules: SLV-39
  // expect: silver
  explicit Pair(int x) : Pair(x, x) {}
};

// rules: SLV-39
// expect: silver
int same(int x) {
  PORK_POST(int r, r == 0);
  Pair p(x);
  return p.a - p.b == 0 ? 0 : 1;
}

// Invarianten ska gälla efter konstruktorn (men inte före).
struct Range {
  int lo;
  int hi;
  // rules: SLV-39, SLV-19
  // expect: silver
  PORK_INVARIANT(lo <= hi);
  // rules: SLV-39, SLV-19
  // expect: silver
  Range(int a, int b) : lo(a < b ? a : b), hi(a < b ? b : a) {}
  // rules: SLV-39, SLV-19
  // expect: stone: not silver: invariant of 'Range' on exit from 'Range::Range'
  explicit Range(int a) : lo(a), hi(0) {}
};

// Kontextreferens bunden till en T&-parameter.
struct Context {
  int x;
};
struct Reader {
  Context& game_;
  int seen = 0;
  // rules: SLV-39, SLV-36
  // expect: silver
  explicit Reader(Context& g) : game_(g) {}
  // rules: SLV-36
  // expect: silver
  int get() const { return game_.x; }
};

// Basinitierare med fält: inte än.
struct Base {
  int n;
  // rules: SLV-39
  // expect: silver
  explicit Base(int k) : n(k) {}
};
struct Derived : Base {
  // rules: SLV-39
  // expect: stone: not silver: cannot translate to C: base initializer (not translated yet)
  Derived() : Base(3) {}
};

// Destruktorer översätts inte än (STN-68): det som kör en egen destruktor är
// inte Silver, i stället för att destruktorn tyst faller bort.
struct Guard {
  int v = 0;
  // rules: STN-68
  // expect: stone: not silver: cannot translate to C: destructor (not translated yet)
  ~Guard() { v = 1; }
};

// rules: STN-68
// expect: stone: not silver: cannot translate 'Guard::~Guard' to C: destructor (not translated yet)
int guarded() {
  Guard g;
  return g.v;
}
