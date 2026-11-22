int state = 0;

// rules: STN-23
// expect: stone
int square(int a) { return a * a; }

// rules: STN-22
// expect: not-stone: uses variable 'state'
int impure_b(int a) { state += a; return state; }
