#include "shared.hpp"

int counter = 0;

// rules: STN-94
// expect: stone
int same_in_both(int x) { return twice(x) + Box{x}.doubled() + pick(1, 2); }

// rules: STN-94
// expect: not-stone: calls non-stone 'base'
int uses_base_a() { return base(); }
