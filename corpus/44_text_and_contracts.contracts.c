#include "pork_contracts.h"

// round: r med |r - x| <= 0.5.
double round__double(double x) {
  double r = nondet_double();
  __CPROVER_assume(PORK_FINITE_D(r) && r - x <= 0.5 && x - r <= 0.5);
  return r;
}
