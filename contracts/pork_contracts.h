#pragma once

// Hjälpmedel för kontraktsfiler (contracts/std.c och egna via --contracts):
//
//   #include "pork_contracts.h"
//   double round__double(double x) {
//     double r = nondet_double();
//     __CPROVER_assume(PORK_FINITE_D(r) && r - x <= 0.5 && x - r <= 0.5);
//     return r;
//   }
//
// pork lägger till katalogen som include-sökväg för bevisaren.

#define PORK_FINITE_F(x) (!__CPROVER_isnanf(x) && !__CPROVER_isinff(x))
#define PORK_FINITE_D(x) (!__CPROVER_isnand(x) && !__CPROVER_isinfd(x))

float nondet_float(void);
double nondet_double(void);
int nondet_int(void);
long nondet_long(void);
unsigned long nondet_ulong(void);
