// pork-args: --silver
//
// assert(c) från <cassert> (Assert.hpp):
//  - Stone: villkoret ska vara ett rent uttryck; resten av makrot (sizeof,
//    __assert_fail) är libc:s och granskas inte.
//  - Först i kroppen (före eller bland PORK_PRE/PORK_POST): förvillkor, som
//    GNAT:s pragma Precondition. Antas i funktionen, kontrolleras vid anrop.
//  - Annars: en kontroll (som SPARK:s pragma Assert) som ska bevisas.
// Med NDEBUG finns inget assert kvar att se.

#include <cassert>
#include <pork/annotations.hpp>

int counter = 0;

// --- Stone ---

// rules: STN-49
// expect: not-stone: side effect inside expression: ++
int effect_in_assert(int a) { assert(a++ > 0); return a; }

// rules: STN-49, STN-22
// expect: not-stone: uses variable 'counter' with static storage
int global_in_assert(int a) { assert(a < counter); return a; }

// rules: STN-49, STN-23
// expect: silver
bool positive(int a) { return a > 0; }

// --- Inledande assert: förvillkor ---

// rules: STN-49, SLV-32
// expect: silver
int half(int a) { assert(a >= 0); return a / 2; }

// rules: SLV-32, SLV-17
// expect: stone: not silver: precondition of 'half': assert(a >= 0)
int half_any(int a) { return half(a); }

// rules: SLV-32
// expect: silver
int half_checked(int a) { if (a < 0) return 0; return half(a); }

// rules: STN-49, SLV-32, SLV-17
// expect: silver
int quotient(int a, int b) { assert(a >= 0); PORK_PRE(b > 0); assert(b < 100); return a / b; }

// rules: SLV-32, SLV-4
// expect: silver
int calls_in_assert(int a) { assert(positive(a)); return a - 1; }

// rules: SLV-32, SLV-20
// expect: stone: not silver: preconditions can never hold
int impossible(int a) { assert(a > 0); assert(a < 0); return a * a; }

// --- Assert längre ner: kontroll ---

// rules: SLV-33
// expect: stone: not silver: assertion 'a > 0' at 34_assert.cpp:60
int may_fail(int a) { assert(a > 0); a = a - 1; assert(a > 0); return a; }

// rules: SLV-33
// expect: silver
int holds(int a) { assert(a > 0 && a < 100); a = a + 1; assert(a > 1); return a; }

// rules: SLV-33
// expect: silver
int in_branch(int a) { if (a > 10) { assert(a != 0); return 100 / a; } return 0; }

// rules: SLV-33
// expect: stone: not silver: assertion 'a != 5'
int in_loop(int n) { for (int a = 0; a < n && a < 10; ++a) assert(a != 5); return n; }
