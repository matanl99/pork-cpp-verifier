// pork-args: --silver
//
// Rekursion genom kontrakt (SLV-18). En anropare med PORK_POST-kontrakt tar
// inte med koden, så två funktioner som anropar varandra bevisas var för sig
// mot den andras eftervillkor. Det visar att rekursionen är konsekvent, inte
// att den tar slut: f(n) och g(n) anropar varandra för alltid.

#include <pork/annotations.hpp>

int g(int n);

// rules: SLV-18
// expect: stone: not silver: relies on the contract of 'g', which leads back to 'f': recursion through contracts is not proved to terminate
int f(int n) {
  PORK_POST(int r, r == 1);
  return g(n);
}

// rules: SLV-18
// expect: stone: not silver: relies on the contract of 'f', which leads back to 'g': recursion through contracts is not proved to terminate
int g(int n) {
  PORK_POST(int r, r == 1);
  return f(n);
}

// rules: SLV-18
// expect: stone: not silver: relies on the unproven postcondition of 'f'
int use(int n) { return 100 / f(n); }

// Tre led: a -> b -> c -> a.
int b3(int n);
int c3(int n);

// rules: SLV-18
// expect: stone: not silver: relies on the contract of 'b3', which leads back to 'a3': recursion through contracts is not proved to terminate
int a3(int n) {
  PORK_POST(int r, r >= 0);
  return b3(n);
}

// rules: SLV-18
// expect: stone: not silver: relies on the contract of 'c3', which leads back to 'b3': recursion through contracts is not proved to terminate
int b3(int n) {
  PORK_POST(int r, r >= 0);
  return c3(n);
}

// rules: SLV-18
// expect: stone: not silver: relies on the contract of 'a3', which leads back to 'c3': recursion through contracts is not proved to terminate
int c3(int n) {
  PORK_POST(int r, r >= 0);
  return a3(n);
}

// En kedja utan cykel bygger fortfarande på kontrakten.
// rules: SLV-18
// expect: silver
int leaf(int n) {
  PORK_PRE(n >= 0 && n < 100);
  PORK_POST(int r, r >= 1);
  return n + 1;
}

// rules: SLV-18
// expect: silver
int middle(int n) {
  PORK_PRE(n >= 0 && n < 100);
  PORK_POST(int r, r >= 1);
  return leaf(n);
}

// rules: SLV-18
// expect: silver
int top(int n) {
  PORK_PRE(n >= 0 && n < 100);
  return 100 / middle(n);
}

// Rekursion genom en funktion utan kontrakt: koden ingår i beviset, och
// rekursionen begränsas av --unwind (SLV-16).
int through(int n);

// rules: SLV-16, SLV-18
// expect: stone: not silver: loop may need more than 64 iterations (raise --unwind) at 99_silver_contract_cycles.cpp:88
// expect[esbmc]: stone: not silver: postcondition of 'with_post' at 99_silver_contract_cycles.cpp:88
int with_post(int n) {
  PORK_POST(int r, r == 1);
  return through(n);
}

// rules: SLV-16
// expect: stone: not silver: relies on the unproven postcondition of 'with_post'
int through(int n) { return with_post(n); }
