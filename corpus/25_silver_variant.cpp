// pork-args: --silver --vector-bound 4
//
// std::variant, get_if och lambdor i Silver:
//  - std::variant<A, B> blir { index; union { A a0; B a1; } u; }.
//  - std::get<T> kräver att T är det aktiva alternativet.
//  - get_if ger en pekare (&v.u.ak eller 0); x->f kontrolleras mot null.
//  - En lambda blir en C-funktion med fångsterna som extra parametrar
//    (pekare för fångst med referens). std::visit blir ett val på index.

#include <pork/annotations.hpp>

#include <optional>
#include <variant>
#include <vector>

struct Vec2 { float x{0.0F}; float y{0.0F}; };
struct RunTo { int player{0}; Vec2 target; };
struct PassTo { int player{0}; int receiver{0}; };
using Command = std::variant<RunTo, PassTo>;
struct Planned { int offset{0}; Command command; };

// rules: SLV-25
// expect: silver: assuming
bool is_pass(const Command& c) { return std::holds_alternative<PassTo>(c); }

// rules: SLV-25
// expect: silver: assuming
unsigned long kind(const Command& c) { return c.index(); }

// rules: SLV-25
// expect: silver: assuming
Command make_pass(int a, int b) { return PassTo{a, b}; }

// rules: SLV-25
// expect: silver: assuming
int made_pass_receiver(int a) { const Command c = make_pass(a, 7); return std::get<PassTo>(c).receiver; }

// rules: SLV-25
// expect: stone: not silver: precondition of 'std::get'
int receiver_any(const Command& c) { return std::get<PassTo>(c).receiver; }

// rules: SLV-25
// expect: silver: assuming
void to_run(Command& c, Vec2 t) { c = RunTo{1, t}; }

// rules: SLV-25, SLV-12
// expect: silver: assuming
int passer(const Planned& p) {
  if (const auto* pass = std::get_if<PassTo>(&p.command)) return pass->player;
  return -1;
}

// rules: SLV-12
// expect: stone: not silver: dereference failure: pointer NULL
int passer_unchecked(const Planned& p) {
  const auto* pass = std::get_if<PassTo>(&p.command);
  return pass->player;
}

// rules: SLV-25
// expect: silver: assuming
void retarget(Planned& p, Vec2 t) {
  if (auto* run = std::get_if<RunTo>(&p.command)) run->target = t;
}

// rules: SLV-25
// expect: silver: assuming
int player_of(const Command& c) {
  return std::visit([](const auto& cmd) { return cmd.player; }, c);
}

// rules: SLV-5, SLV-25
// expect: stone: not silver: arithmetic overflow on signed +
int sum_players(const Command& c, int n) {
  return std::visit([n](const auto& cmd) { return cmd.player + n; }, c);
}

// rules: SLV-17
// expect: silver
int add(int a, int b) { PORK_PRE(a >= 0 && a < 1000 && b >= 0 && b < 1000); return a + b; }

// rules: SLV-25
// expect: silver
int count_up(int n) {
  PORK_PRE(n >= 0 && n < 100);
  int total = 0;
  auto bump = [&](int by) { total = add(total, by); };
  bump(n);
  bump(1);
  return total;
}

// rules: SLV-17
// expect: stone: not silver: precondition of 'add'
int count_far(int n) {
  PORK_PRE(n >= 0 && n < 1000);
  int total = 0;
  auto bump = [&](int by) { total = add(total, by); };
  bump(n);
  bump(n);
  bump(n);  // 2n kan vara >= 1000
  return total;
}

// rules: SLV-25
// expect: silver
float scaled(Vec2 v) {
  PORK_PRE(v.x > -10.0F && v.x < 10.0F && v.y > -10.0F && v.y < 10.0F);
  float k = 2.0F;
  auto f = [k](float x) { return x * k; };
  // expect-warning: the assignment to 'k' has no effect
  k = 1e30F;  // fångsten är en kopia från när lambdan skapades
  return f(v.x) + f(v.y);
}
