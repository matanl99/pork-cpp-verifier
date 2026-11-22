// pork-args: --silver
//
// Gränssnittskontrakt i en kontraktsfil (här samma fil): en fri funktion per
// metod, märkt PORK_INTERFACE_CONTRACT, med gränssnittet som första parameter
// och bara PORK_PRE/PORK_POST. Som SPARK:s Pre'Class/Post'Class:
//  - ett dispatchande anrop kontrollerar förvillkoret och använder
//    eftervillkoret, aldrig en implementations kod;
//  - efter anropet har allt utanför funktionen som implementationen kan nå
//    (*this, kontexten, referensparametrarna) godtyckliga värden (STN-54);
//  - varje känd implementation bevisas uppfylla kontraktet (Liskov) och ärver
//    förvillkoret.

#include <pork/annotations.hpp>

struct Context { int x; int dx; };

struct IClock {
  // rules: STN-50
  // expect: silver
  virtual ~IClock() = default;
  virtual int now() const = 0;
  virtual void advance(int& t) = 0;
};

#pragma clang diagnostic ignored "-Wreturn-type"
// rules: STN-56
// expect: stone
PORK_INTERFACE_CONTRACT int now(const IClock& self) { PORK_POST(int r, r >= 0 && r < 1000); }
// rules: STN-56
// expect: stone
PORK_INTERFACE_CONTRACT void advance(IClock& self, int& t) { PORK_PRE(t >= 0 && t < 100); PORK_POST(void, t >= 0 && t < 200); }

struct Fixed final : IClock {
  // rules: SLV-38
  // expect: silver
  int now() const override { return 7; }
  // Ärver förvillkoret t < 100: inget spill.
  // rules: SLV-38
  // expect: silver
  void advance(int& t) override { t = t + 1; }
};

// rules: SLV-37, SLV-17
// expect: silver: assuming dispatch IClock::now
int elapsed(const IClock& c, int start) { PORK_PRE(start >= 0 && start < 1000); int n = c.now(); return n - start; }

// rules: SLV-37
// expect: silver: assuming dispatch IClock::now
int sum(const IClock& c) { int a = c.now(); int b = c.now(); return a + b; }

// rules: SLV-37
// expect: silver: assuming dispatch IClock::advance
int step(IClock& c) { int t = 5; c.advance(t); return t * 2; }

// Förvillkoret kontrolleras vid anropet.
// rules: SLV-37
// expect: stone: not silver: precondition of 'IClock::advance'
int step_far(IClock& c) { int t = 500; c.advance(t); return t; }

// Kontexten kan ha ändrats under anropet.
struct User {
  IClock& clock_;
  Context& game_;
  // rules: SLV-37, STN-54
  // expect: stone: not silver: division by zero
  int tick() { PORK_PRE(game_.x == 1); int n = clock_.now(); return n + 10 / game_.x; }
  // rules: SLV-37
  // expect: silver: assuming dispatch IClock::now
  int tick_before() { PORK_PRE(game_.x == 1); int q = 10 / game_.x; int n = clock_.now(); return n + q; }
};

// Ett direkt anrop kontrollerar det ärvda förvillkoret.
// rules: SLV-38
// expect: stone: not silver: precondition of the contract of 'IClock::advance'
void direct(Fixed& f) { int t = 100; f.advance(t); }
