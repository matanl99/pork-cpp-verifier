#define USE_COUNTER
#include "shared.hpp"

// rules: STN-94
// expect: stone
long other_instance(long x) { return pick(x, 2L) + twice(3); }

// Här läser base() en global: med en dom från a.cpp:s definition vore den Stone.
// rules: STN-94
// expect: not-stone: calls non-stone 'base'
int uses_base_b() { return base(); }
