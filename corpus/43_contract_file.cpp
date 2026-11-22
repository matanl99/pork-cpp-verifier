// Regler för kontraktsfilen (STN-56).

#include <pork/annotations.hpp>

struct IPump {
  virtual int rate() const = 0;
  // Kontrakt både i definitionen och i kontraktsfilen.
  // rules: STN-56
  // expect: stone
  virtual int burst(int n) { PORK_PRE(n > 0); return n; }
  // Samma sak med PORK_DEPENDS före förvillkoret: hela kontraktsinledningen
  // räknas, inte bara första satsen.
  // rules: STN-56, BRZ-1
  // expect: bronze
  virtual int surge(int n) {
    PORK_DEPENDS_RESULT(n);
    PORK_PRE(n > 0);
    return n;
  }
};

#pragma clang diagnostic ignored "-Wreturn-type"

// const-metod: gränssnittet som const&.
// rules: STN-56
// expect: not-stone: no virtual method 'IPump::rate' with this signature
PORK_INTERFACE_CONTRACT int rate(IPump& self) { PORK_POST(int r, r > 0); }

// rules: STN-56
// expect: not-stone: an interface contract may only contain PORK_PRE and PORK_POST
PORK_INTERFACE_CONTRACT int rate(const IPump& self) { PORK_POST(int r, r > 0); return 1; }

// rules: STN-56
// expect: not-stone: 'IPump::burst' has a contract in its definition and in the contract file
PORK_INTERFACE_CONTRACT int burst(IPump& self, int n) { PORK_PRE(n > 1); }

// rules: STN-56
// expect: not-stone: 'IPump::surge' has a contract in its definition and in the contract file
PORK_INTERFACE_CONTRACT int surge(IPump& self, int n) { PORK_PRE(n > 1); }
