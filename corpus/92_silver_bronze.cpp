// BRZ-4: nivåerna är kumulativa. Ett krav som håller ger silver när beviset
// går igenom; ett brutet krav fäller Silver trots beviset.
// pork-args: --silver
#include <pork/annotations.hpp>

// rules: BRZ-4, SLV-1
// expect: silver
int invoice(int price, bool discount, int& audit) {
  PORK_PRE(price >= 0 && price < 1000);
  PORK_DEPENDS(audit, price);
  audit = price;
  return discount ? price / 2 : price;
}

// rules: BRZ-4
// expect: stone: not bronze: audit depends on discount, which PORK_DEPENDS does not allow
// expect-violation
int invoice_mutated(int price, bool discount, int& audit) {
  PORK_PRE(price >= 0 && price < 1000);
  PORK_DEPENDS(audit, price);
  audit = discount ? price / 2 : price;
  return discount ? price / 2 : price;
}

// Fält i *this (BRZ-1): totalen får bero på sig själv och x, revisionen bara på x.
struct Ledger {
  int total = 0;
  int audit = 0;
  // rules: BRZ-1, BRZ-4
  // expect: silver
  void add(int x) {
    PORK_PRE(x >= 0 && x < 100 && total >= 0 && total < 1000);
    PORK_DEPENDS(total, total, x);
    PORK_DEPENDS(audit, x);
    total += x;
    audit = x;
  }
  // rules: BRZ-1, BRZ-4
  // expect: stone: not bronze: this.audit depends on this.total, which PORK_DEPENDS does not allow
  // expect-violation
  void add_mutated(int x) {
    PORK_PRE(x >= 0 && x < 100 && total >= 0 && total < 1000);
    PORK_DEPENDS(audit, x);
    total += x;
    audit = total;
  }
};

// Ett brutet krav fäller också anropare som bygger på eftervillkoret: de
// modulära beroendena kräver slutlig Silver (Bronze avgörs före fixpunkten).
// rules: BRZ-4
// expect: stone: not bronze: the result depends on bonus, which PORK_DEPENDS does not allow
// expect-violation
int capped(int x, int bonus) {
  PORK_PRE(x >= 0 && x <= 100 && bonus >= 0 && bonus <= 1);
  PORK_POST(int r, r >= 0 && r <= 101);
  PORK_DEPENDS_RESULT(x);
  return x + bonus;
}
// rules: BRZ-4, SLV-1
// expect: stone: not silver: relies on the postcondition of 'capped', which is not bronze
int uses_capped(int x) {
  PORK_PRE(x >= 0 && x <= 100);
  return capped(x, 1) + 1;
}
