// Former på satsnivå (STN-13, STN-19, STN-18, STN-20): den tomma satsen `;`
// och en cast till void som egen sats (`(void)x;`, `static_cast<void>(f(x));`)
// godtas. Kommaoperatorn godtas bara som kedja av satser (också i ett
// for-steg), inte inuti ett annat uttryck.

// rules: STN-13
// expect: stone
void empty_statement() {
  ;
}

// rules: STN-13
// expect: stone
int empty_loop_body(int n) {
  int i = 0;
  for (; i < n; ++i)
    ;
  return i;
}

// rules: STN-19
// expect: stone
void void_cast(int n) {
  static_cast<void>(n);
  (void)n;
}

// rules: STN-19, STN-20
// expect: stone
int void_cast_of_call(int n) {
  (void)empty_loop_body(n);
  return n;
}

// rules: STN-20
// expect: stone
int statement_comma(int n) {
  int a = 0;
  int b = 0;
  a = n, b = n + 1;
  for (int i = 0, j = n; i < j; ++i, --j) a = a + 1;
  return a + b;
}

// --- avvisas ------------------------------------------------------------------

// rules: STN-18
// expect: not-stone: comma operator inside an expression
int comma_in_return(int n) {
  return (static_cast<void>(n), 2);
}

// rules: STN-18
// expect: not-stone: comma operator inside an expression
int comma_in_initializer(int n) {
  int k = (static_cast<void>(n), n + 1);
  return k;
}

// rules: STN-18
// expect: not-stone: comma operator inside an expression
int comma_in_condition(int n) {
  if ((void)n, n > 0) return 1;
  return 0;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"empty_statement", [] { empty_statement(); return 0; }},
    {"empty_loop_body", [] { return empty_loop_body(3) == 3 ? 0 : 1; }},
    {"void_cast", [] { void_cast(1); return 0; }},
    {"void_cast_of_call", [] { return void_cast_of_call(2) == 2 ? 0 : 1; }},
    {"statement_comma", [] { return statement_comma(4) == 11 ? 0 : 1; }})
#endif
