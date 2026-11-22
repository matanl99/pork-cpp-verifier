// pork-args: --silver
//
// Liskov: en implementation som inte uppfyller gränssnittets kontrakt är
// inte Silver, och anropare som bygger på kontraktet inte heller.

#include <pork/annotations.hpp>

struct IGauge {
  // rules: STN-50
  // expect: silver
  virtual ~IGauge() = default;
  virtual int level() const = 0;
  virtual void fill(int& v) = 0;
  virtual int spare() const = 0;
};

#pragma clang diagnostic ignored "-Wreturn-type"
// rules: STN-56
// expect: stone
PORK_INTERFACE_CONTRACT int level(const IGauge& self) { PORK_POST(int r, r >= 0); }
// rules: STN-56
// expect: stone
PORK_INTERFACE_CONTRACT void fill(IGauge& self, int& v) { PORK_PRE(v >= 0 && v < 10); PORK_POST(void, v < 20); }

struct Leaky final : IGauge {
  // Eftervillkoret håller inte.
  // rules: SLV-38
  // expect: stone: not silver: does not satisfy the contract of 'IGauge::level': postcondition of the contract of 'IGauge::level'
  int level() const override { return -1; }
  // Kräver mer än kontraktet.
  // rules: SLV-38
  // expect: stone: not silver: does not satisfy the contract of 'IGauge::fill': precondition of 'Leaky::fill'
  void fill(int& v) override { PORK_PRE(v > 5); v = v + 1; }
  // Inget kontrakt: bara den egna koden.
  // rules: SLV-2
  // expect: silver
  int spare() const override { return 0; }
};

// rules: SLV-37, SLV-38
// expect: stone: not silver: relies on the contract of 'IGauge::level', and the implementation 'Leaky::level' is not silver
int read(const IGauge& g) { int n = g.level(); return n; }

// rules: SLV-37, SLV-21
// expect: stone: not silver: no contract for 'IGauge::spare (interface contract)'
int read_spare(const IGauge& g) { int n = g.spare(); return n; }
