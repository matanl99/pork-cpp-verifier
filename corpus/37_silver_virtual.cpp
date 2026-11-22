// pork-args: --silver
//
// Virtuella funktioner i Silver. Dispatchande anrop kräver gränssnittets
// kontrakt (41_silver_contracts.cpp). Även om basens kropp finns i samma
// program binds anropet aldrig till den.

struct IClock {
  // rules: STN-8, STN-50
  // expect: silver
  virtual ~IClock() = default;
  // rules: SLV-2
  // expect: silver
  virtual int now() const { return 0; }
};

struct FixedClock final : IClock {
  // rules: SLV-2
  // expect: silver
  int now() const override { return 7; }
};

// final och kvalificerat anrop: vanliga anrop.
// rules: SLV-2, SLV-4
// expect: silver
int direct(const FixedClock& f, const IClock& c) { return f.now() + c.IClock::now(); }

// rules: SLV-2, SLV-34
// expect: stone: not silver: no contract for 'IClock::now (interface contract)'
int both(const FixedClock& f, const IClock& c) { int n = c.now(); return f.now() + c.IClock::now() + (n > 0 ? 1 : 0); }
