// Förvillkor: PORK_PRE(villkor) som första satser i kroppen (som SPARK Pre).
//  - Villkoret är ett rent uttryck (Stone-reglerna gäller, anrop till
//    Stone-funktioner går bra).
//  - Bara först i kroppen: efter en annan sats är det inget förvillkor.
//  - pork::pre själv rapporteras inte.
// På Stone-nivå ändrar förvillkor ingenting; Silver antar dem i funktionen
// och kontrollerar dem vid varje anrop.

#include <pork/annotations.hpp>

int g = 0;

// rules: STN-42
// expect: stone
int halve(int a) { PORK_PRE(a >= 0); return a / 2; }

// rules: STN-42
// expect: stone
float speed(float d, float t) { PORK_PRE(t > 0.0F); PORK_PRE(d >= 0.0F && d < 1000.0F); return d / t; }

// rules: STN-1
// expect: stone
bool in_range(int a) { return a >= 0 && a < 100; }

// rules: STN-42, STN-23
// expect: stone
int checked(int a) { PORK_PRE(in_range(a)); return a * 2; }

// rules: STN-42
// expect: stone
int empty_body_after_pre(int a) { PORK_PRE(a != 0); return 1; }

// rules: STN-42
// expect: not-stone: PORK_PRE must come before all other statements
int late(int a) { int b = a + 1; PORK_PRE(b > 0); return b; }

// rules: STN-42
// expect: not-stone: PORK_PRE must come before all other statements
int nested(int a) { if (a > 0) { PORK_PRE(a < 10); } return a; }

// rules: STN-42, STN-20
// expect: not-stone: side effect inside expression
int effect(int a) { PORK_PRE(a++ > 0); return a; }

// rules: STN-42, STN-22
// expect: not-stone: uses variable 'g' with static storage
int reads_global(int a) { PORK_PRE(a < g); return a; }
