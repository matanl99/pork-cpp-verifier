// pork-args: --silver
//
// Självtest av kontrakten: förvillkor (PORK_PRE och invarianter) som aldrig
// kan gälla samtidigt gör varje bevis tomt (allt följer ur något falskt).
// Silver kontrollerar därför att det finns minst en indata som uppfyller dem.

#include <pork/annotations.hpp>

// Utan kontrollen: silver, trots overflow.
// rules: SLV-20
// expect: stone: not silver: preconditions can never hold
int impossible(int x) {
  PORK_PRE(x > 5 && x < 3);
  return x * 1000000000;
}

// Motsägelse mellan två förvillkor.
// rules: SLV-20
// expect: stone: not silver: preconditions can never hold
int split_contradiction(int x) {
  PORK_PRE(x > 5);
  PORK_PRE(x < 3);
  return x * 1000000000;
}

// Flyttal antas ändliga, så ett NaN-krav kan aldrig uppfyllas.
// rules: SLV-20, SLV-22
// expect: stone: not silver: preconditions can never hold
float only_nan(float f) {
  PORK_PRE(f != f);
  return f * 1.0e30F * 1.0e30F;
}

struct Never {
  int n;
  // rules: STN-44
  // expect: silver
  PORK_INVARIANT(n > 5 && n < 3);
};

// rules: SLV-20, SLV-19
// expect: stone: not silver: preconditions can never hold
int never_value(const Never& v) { return v.n * 1000000000; }

// En funktion som ska skapa en Never kan inte uppfylla invarianten.
// rules: SLV-19
// expect: stone: not silver: invariant of 'Never' on exit from 'make_never'
Never make_never() { return {4}; }

// Ett förvillkor som kan gälla påverkas inte.
// rules: SLV-20
// expect: silver
int fine(int x, int y) {
  PORK_PRE(x >= 0 && x <= 10 && y >= 1 && y <= 5);
  return x / y;
}
