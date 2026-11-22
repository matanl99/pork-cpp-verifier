// Två översättningsenheter i samma körning: anrop till b.cpp.

int square(int a);
int impure_b(int a);

// rules: STN-23
// expect: stone
int sum_of_squares(int a, int b) { return square(a) + square(b); }

// rules: STN-23
// expect: not-stone: calls non-stone 'impure_b'
int uses_impure_b(int a) { return impure_b(a); }
