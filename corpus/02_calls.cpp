// Anrop: Stone är transitivt.

int g = 0;

int declared_only(int a);

// rules: STN-23
// expect: stone
int leaf(int a) { return a * 2; }

// rules: STN-22
// expect: not-stone: uses variable 'g'
int impure(int a) { return a + g; }

// rules: STN-23
// expect: stone
int calls_leaf(int a) { return leaf(a) + 1; }

// rules: STN-23
// expect: not-stone: calls non-stone 'impure'
int calls_impure(int a) { return impure(a); }

// rules: STN-23
// expect: not-stone: calls non-stone 'calls_impure'
int calls_impure_indirectly(int a) { return calls_impure(a); }

// rules: STN-23
// expect: not-stone: with no definition in the analysed sources
int calls_declared_only(int a) { return declared_only(a); }

int is_odd(unsigned n);

// rules: STN-23
// expect: stone
int is_even(unsigned n) { return n == 0 ? 1 : is_odd(n - 1); }

// rules: STN-23
// expect: stone
int is_odd(unsigned n) { return n == 0 ? 0 : is_even(n - 1); }

// rules: STN-12
// expect: not-stone: parameter 'f' has type int (*)(int)
int apply(int (*f)(int), int a) { return f(a); }

// rules: STN-30
// expect: stone
int default_arg(int a, int b = 3) { return a + b; }

// rules: STN-30
// expect: stone
int uses_default_arg(int a) { return default_arg(a); }

// rules: STN-30
// expect: stone
int global_default(int a, int b = g + 0 * 0) { return a + b; }

// rules: STN-30, STN-22
// expect: not-stone: uses variable 'g'
int uses_global_default(int a) { return global_default(a); }
