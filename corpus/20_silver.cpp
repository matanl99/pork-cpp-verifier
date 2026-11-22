// pork-args: --silver
//
// Silver: frånvaro av körtidsfel (som SPARK Silver), bevisat med CBMC för alla
// indata som uppfyller förvillkoren (PORK_PRE). Bara Stone-funktioner prövas.
// Kontroller: heltals-overflow (signed), division med noll, odefinierade
// skift, flyttals-overflow och NaN, förvillkor vid anrop, loopar som inte
// bevisats ta slut inom --unwind varv.
//
//   silver                         bevisad
//   stone: not silver: <orsak>     Stone, men ett körtidsfel är möjligt
//
// Flyttalsparametrar antas vara ändliga (inte NaN/inf); enum-parametrar antas
// ha ett av sina uppräknade värden.

#include <pork/annotations.hpp>

// --- Heltal ---

// rules: SLV-5
// expect: silver
int add_small(short a, short b) { return a + b; }

// rules: SLV-5
// expect: stone: not silver: arithmetic overflow on signed +
int add(int a, int b) { return a + b; }

// rules: SLV-5, SLV-17
// expect: silver
int add_pre(int a, int b) { PORK_PRE(a >= 0 && a < 1000 && b >= 0 && b < 1000); return a + b; }

// rules: SLV-5, SLV-29
// expect: silver
unsigned wrap(unsigned a, unsigned b) { return a + b; }

// rules: SLV-6
// expect: stone: not silver: division by zero
int quotient(int a, int b) { PORK_PRE(a >= 0); return a / b; }

// rules: SLV-6, SLV-17
// expect: silver
int quotient_pre(int a, int b) { PORK_PRE(a >= 0 && b != 0); return a / b; }

// rules: SLV-8
// expect: stone: not silver: shift distance
int shl(int a, int n) { PORK_PRE(a >= 0 && a < 16); return a << n; }

// rules: SLV-8, SLV-17
// expect: silver
int shl_pre(int a, int n) { PORK_PRE(a >= 0 && a < 16 && n >= 0 && n < 8); return a << n; }

// --- Anrop: förvillkoret kontrolleras hos anroparen ---

// rules: SLV-17
// expect: silver
int half(int a) { PORK_PRE(a >= 0); return a / 2; }

// rules: SLV-17
// expect: stone: not silver: precondition of 'half'
int half_any(int a) { return half(a); }

// rules: SLV-17
// expect: silver
int half_abs(int a) { PORK_PRE(a > -1000 && a < 1000); return half(a < 0 ? -a : a); }

// Fel i en anropad funktion är fel även hos anroparen.
// rules: SLV-4, SLV-5
// expect: stone: not silver: arithmetic overflow on signed +
int twice(int a) { return add(a, a); }

// rules: SLV-4, SLV-17
// expect: silver
int twice_pre(int a) { PORK_PRE(a > -1000 && a < 1000); return add(a, a); }

// --- Flyttal och structar ---

struct Vec2 { float x; float y; };

// rules: SLV-22
// expect: silver
bool small(Vec2 v) { return v.x > -1e6F && v.x < 1e6F && v.y > -1e6F && v.y < 1e6F; }

// rules: SLV-9
// expect: stone: not silver: arithmetic overflow on floating-point addition
Vec2 vadd(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }

// rules: SLV-9, SLV-17
// expect: silver
Vec2 vadd_pre(Vec2 a, Vec2 b) { PORK_PRE(small(a) && small(b)); return {a.x + b.x, a.y + b.y}; }

// Nämnaren skild från noll och inte för liten: varken division med noll, NaN
// eller overflow.
// rules: SLV-7, SLV-10, SLV-9
// expect: silver
float ratio_pre(float a, float b) { PORK_PRE(a > -1.0F && a < 1.0F && b >= 0.5F && b < 1.0F); return a / b; }

// ESBMC saknar egen kontroll för flyttalsdivision med noll; 0/0 blir NaN.
// rules: SLV-7, SLV-10, SLV-31
// expect: stone: not silver: floating-point division by zero in a / b
// expect[esbmc]: stone: not silver: NaN on division
float ratio(float a, float b) { PORK_PRE(a > -1.0F && a < 1.0F && b > -1.0F && b < 1.0F); return a / b; }

// Som rugby::velocity_of: d > 0 räcker inte, d kan vara subnormal.
// rules: SLV-9
// expect: stone: not silver: arithmetic overflow on floating-point division
float inverse(float d, float s) { PORK_PRE(d >= 0.0F && d < 1000.0F && s >= 0.0F && s <= 20.0F); if (d <= 0.0F) return 0.0F; return s / d; }

// rules: SLV-9
// expect: silver
float inverse_fixed(float d, float s) { PORK_PRE(d >= 0.0F && d < 1000.0F && s >= 0.0F && s <= 20.0F); if (d <= 1e-6F) return 0.0F; return s / d; }

constexpr float tick = 1.0F / 60.0F;

// rules: SLV-9, SLV-22
// expect: silver
float per_tick(float v) { PORK_PRE(v >= 0.0F && v <= 100.0F); return v * tick; }

// --- Metoder och referenser ---

struct Counter {
  int n;
  // rules: SLV-5
  // expect: stone: not silver: arithmetic overflow on signed +
  void inc() { n = n + 1; }
  // rules: SLV-5, SLV-17
  // expect: silver
  void inc_pre() { PORK_PRE(n < 1000); n = n + 1; }
  // rules: SLV-2
  // expect: silver
  int get() const { return n; }
};

// rules: SLV-4, SLV-17
// expect: silver
int count_twice(int start) { PORK_PRE(start >= 0 && start < 100); Counter c{start}; c.inc_pre(); c.inc_pre(); return c.get(); }

// rules: SLV-2
// expect: silver
void clamp_to(int& x, int hi) { if (x > hi) x = hi; }

// rules: SLV-4
// expect: silver
int clamped(int a) { int x = a; clamp_to(x, 100); return x; }

// rules: SLV-2
// expect: silver
float ref_alias(Vec2 v) { PORK_PRE(small(v)); const Vec2& r = v; return r.x + r.y; }

// rules: SLV-17
// expect: silver
void move_right(Vec2& p) { PORK_PRE(small(p)); p.x += 1.0F; }

// rules: SLV-17
// expect: stone: not silver: precondition of 'move_right'
void move_any(Vec2& p) { move_right(p); }

// --- Enum, switch och loopar ---

enum class Side { home, away };

// rules: SLV-22
// expect: silver
int direction(Side s) { switch (s) { case Side::home: return 1; case Side::away: return -1; } return 0; }

// rules: SLV-16
// expect: silver
int sum_to(int n) { PORK_PRE(n >= 0 && n <= 10); int s = 0; for (int i = 0; i < n; ++i) s += i; return s; }

// rules: SLV-16
// expect: stone: not silver: loop may need more than
int count_up(int n) { PORK_PRE(n >= 0); int s = 0; for (int i = 0; i < n; ++i) s = s + 1; return s; }

// Skalär utan initierare som tilldelas före läsning (Stone har kontrollerat
// det): blir en vanlig C-deklaration. Mönstret följer CWE369-fallen i NIST:s
// Juliet Test Suite for C/C++ 1.3 (public domain, CC0 för utländska rättigheter):
// https://doi.org/10.5281/zenodo.4701387
// rules: SLV-6
// expect: stone: not silver: division by zero
int juliet_bad() { int data; data = -1; data = 0; return 100 / data; }

// rules: SLV-6
// expect: silver
int juliet_good_b2g() { int data; data = -1; data = 0; if (data != 0) return 100 / data; return 0; }

// rules: SLV-6
// expect: silver
int juliet_good_g2b() { int data; data = -1; data = 7; return 100 / data; }

// --- Flyttal till heltal ---
// Fyndet ska ha källraden, inte en rad i den genererade C-koden.

// rules: SLV-11
// expect: stone: not silver: float-to-integer conversion out of range at 20_silver.cpp:193
int float_to_int(float f) { return int(f); }

// rules: SLV-11, SLV-17
// expect: silver
int float_to_int_pre(float f) { PORK_PRE(f > -1000.0F && f < 1000.0F); return int(f); }

// --- sizeof ---

// Värdet är känt: 8 * sizeof(unsigned) bitar.
// rules: SLV-2, SLV-8, SLV-17
// expect: silver
unsigned low_bits(unsigned x, int n) { PORK_PRE(n >= 0 && n < 8 * int(sizeof(unsigned))); return x >> n; }
