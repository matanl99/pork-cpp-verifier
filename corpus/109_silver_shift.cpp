// pork-args: --silver
//
// Vänsterskift av signed i C++20 och senare ([expr.shift], SLV-8): a << b är
// a × 2^b modulo 2^N, så ett resultat som inte ryms och ett negativt a är
// definierade. Ett skiftavstånd som är negativt eller minst bredden är
// fortfarande odefinierat. Med --silver-strict räknas överspillet som ett
// ändrat värde (108_silver_strict_narrow.cpp); i C och före C++20 är det
// odefinierat.

// rules: SLV-8
// expect: silver
int top_bit(int a) {
  if (a >= 0 && a < 2) return a << 31;
  return 0;
}

// rules: SLV-8
// expect: silver
int negative_operand(int a) { return a << 1; }

// rules: SLV-8
// expect: stone: not silver: shift distance out of range at 109_silver_shift.cpp:23
int any_distance(int a, int b) { return a << b; }

// rules: SLV-8
// expect: silver
int bounded_distance(int a, int b) {
  if (b >= 0 && b < 32) return a << b;
  return 0;
}

// rules: SLV-8
// expect: silver
short compound_shift(short s, int b) {
  if (b >= 0 && b < 32) s <<= b;
  return s;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"top_bit", [] { return top_bit(1) < 0 ? 0 : 1; }},
    {"negative_operand", [] { return negative_operand(-3) == -6 ? 0 : 1; }},
    {"bounded_distance", [] { return bounded_distance(1, 4) == 16 ? 0 : 1; }},
    {"compound_shift", [] { return compound_shift(1, 3) == 8 ? 0 : 1; }})
#endif
