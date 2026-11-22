// pork-args: --silver
//
// Eftervillkor och modulärt bevis (som SPARK Post):
//  - PORK_POST(R r, c) först i kroppen; c är ett rent uttryck över
//    resultatet r, parametrarna och *this. Void-funktioner: PORK_POST(void, c).
//  - I c är en värde-/const&-parameter ingångsvärdet, en T&-parameter slutvärdet.
//  - Funktionen bevisar sitt eftervillkor vid varje return.
//  - En anropare tar inte med koden: den kontrollerar förvillkoret och får
//    ett godtyckligt resultat som uppfyller eftervillkoret.
//  - En anropare är bara Silver om eftervillkoren den bygger på är bevisade.

#include <pork/annotations.hpp>

// Anrop med sidoeffekt som if-villkor: x har ändrats när grenen körs.
// rules: STN-21, SLV-17
// expect: silver
bool bump_check(int& x) { PORK_PRE(x < 100); x = x + 1; return x > 3; }
// rules: STN-21, SLV-18
// expect: silver
int after_bump(int x) {
  PORK_PRE(x >= 0 && x < 10);
  PORK_POST(int r, r == 1 || (r >= 4 && r <= 10));
  if (bump_check(x)) return x;
  return 1;
}

// --- Eftervillkoret bevisas i funktionen ----------------------------------

// rules: STN-43, SLV-18
// expect: silver
int clamp100(int a) {
  PORK_POST(int r, r >= 0 && r <= 100);
  return a < 0 ? 0 : a > 100 ? 100 : a;
}

// rules: SLV-18
// expect: stone: not silver: postcondition of 'bad_abs'
int bad_abs(int a) {
  PORK_PRE(a > -1000 && a < 1000);
  PORK_POST(int r, r > 0);
  return a < 0 ? -a : a;
}

// Värdeparametrar är ingångsvärden, även om funktionen ändrar dem.
// rules: SLV-18
// expect: silver
int inc(int a) {
  PORK_PRE(a < 100);
  PORK_POST(int r, r == a + 1);
  a = a + 1;
  return a;
}

// T&-parametrar är slutvärden.
// rules: SLV-18
// expect: silver
void bump(int& x) {
  PORK_PRE(x >= 0 && x < 100);
  PORK_POST(void, x >= 1 && x <= 100);
  x = x + 1;
}

// rules: SLV-18
// expect: stone: not silver: postcondition of 'leave_early'
void leave_early(int& x, bool skip) {
  PORK_POST(void, x == 1);
  if (skip) return;
  x = 1;
}

// --- Anropare använder bara kontraktet ------------------------------------

// rules: SLV-18, SLV-4
// expect: silver
int sum_clamped(int a, int b) { return clamp100(a) + clamp100(b); }

// Koden returnerar 5, men kontraktet säger bara r >= 0: modulärt kan det vara 0.
// rules: SLV-18
// expect: silver
int five() {
  PORK_POST(int r, r >= 0);
  return 5;
}

// rules: SLV-18, SLV-6
// expect: stone: not silver: division by zero
int use_five() { return 1000 / five(); }

// rules: SLV-18, SLV-17
// expect: silver
int bump_once(int x) {
  PORK_PRE(x >= 0 && x < 50);
  bump(x);
  return x;
}

// Efter första bump vet anroparen bara x <= 100, så andra anropets förvillkor kan brytas.
// rules: SLV-18, SLV-17
// expect: stone: not silver: precondition of 'bump'
int bump_twice(int x) {
  PORK_PRE(x >= 0 && x < 50);
  bump(x);
  bump(x);
  return x;
}

// Funktioner som kontraktet anropar följer med (men inte resten av kroppen).
// rules: SLV-18
// expect: silver
bool small(int a) { return a >= 0 && a < 10; }

// rules: SLV-18
// expect: silver
int keep_small(int a) {
  PORK_PRE(small(a));
  PORK_POST(int r, small(r));
  return a;
}

// rules: SLV-18
// expect: silver
int use_keep_small() { return 100 / (keep_small(3) + 1); }

// --- Beviset kräver att eftervillkoren är bevisade --------------------------

// rules: SLV-18
// expect: stone: not silver: postcondition of 'lies'
int lies() {
  PORK_POST(int r, r == 1);
  return 2;
}

// rules: SLV-18
// expect: stone: not silver: relies on the unproven postcondition of 'lies'
int use_lies() { return 10 / lies(); }

// --- Metoder -------------------------------------------------------------

struct Counter {
  int n;
  // rules: SLV-18
  // expect: silver
  void step() {
    PORK_PRE(n >= 0 && n < 10);
    PORK_POST(void, n >= 1 && n <= 10);
    n = n + 1;
  }
};

// rules: SLV-18
// expect: silver
int step_counter(Counter c) {
  PORK_PRE(c.n >= 0 && c.n < 10);
  c.step();
  return c.n * 1000;
}

// --- Stone-regler -----------------------------------------------------------

// rules: STN-43, SLV-1
// expect: not-stone: PORK_POST must come before all other statements
int late_post(int a) {
  int b = a;
  PORK_POST(int r, r == b);
  return b;
}

// rules: STN-43, SLV-1
// expect: not-stone: PORK_POST result must have the return type int
int wrong_type(int a) {
  PORK_POST(long r, r == a);
  return a;
}

// rules: STN-43, STN-20, SLV-1
// expect: not-stone: side effect inside expression
int effect_in_post(int& a) {
  PORK_POST(int r, r == a++);
  return a;
}
