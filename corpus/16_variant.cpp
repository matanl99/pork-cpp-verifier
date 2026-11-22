// std::variant, get_if-lån och lambdor.
//
//  - std::variant (betrodd typ) är en värdetyp om alternativen är det.
//  - `T* x = std::get_if<A>(&v)`, med v en väg, är ett lån av v som kan vara
//    null: x får bara jämföras med nullptr, användas som bool, med x->f och *x.
//    Aldrig tilldelas, skickas vidare eller returneras.
//  - Lambdor: lokala (`auto f = [&](...) {...}; f(x)`) och i std::visit.
//    Kroppen granskas för sig. Ett anrop är ett anrop där fångsterna är extra
//    argument: T& om kroppen skriver till dem, annars const T&.

#include <optional>
#include <variant>
#include <vector>

struct Vec2 { float x{0.0F}; float y{0.0F}; };
struct RunTo { int player{0}; Vec2 target; };
struct PassTo { int player{0}; int receiver{0}; };
using Command = std::variant<RunTo, PassTo>;
struct Planned { int offset{0}; Command command; };
struct World { std::vector<Vec2> positions; int passes{0}; };

// --- variant som värdetyp ---------------------------------------------------

// rules: STN-4, STN-24
// expect: stone: assuming
bool is_pass(const Command& c) { return std::holds_alternative<PassTo>(c); }

// rules: STN-4, STN-24
// expect: stone: assuming
unsigned long kind(const Command& c) { return c.index(); }

// rules: STN-4, STN-24
// expect: stone: assuming
Command make_pass(int a, int b) { return PassTo{a, b}; }

// rules: STN-4, STN-24
// expect: stone: assuming
void to_run(Command& c, Vec2 t) { c = RunTo{0, t}; }

// rules: STN-4, STN-24
// expect: stone: assuming
int receiver_of(const Command& c) { return std::get<PassTo>(c).receiver; }

// --- get_if ----------------------------------------------------------------

// rules: STN-39
// expect: stone: assuming
int passer(const Planned& p) {
  if (const auto* pass = std::get_if<PassTo>(&p.command)) return pass->player;
  return -1;
}

// rules: STN-39
// expect: stone: assuming
bool passes_from(const Planned& p, int who) {
  const auto* pass = std::get_if<PassTo>(&p.command);
  return pass != nullptr && pass->player == who;
}

// rules: STN-39
// expect: stone: assuming
void retarget(Planned& p, Vec2 t) {
  if (auto* run = std::get_if<RunTo>(&p.command)) run->target = t;
}

// rules: STN-39, STN-62
// expect: not-stone: loans: changes 'p.command' while 'run' borrows 'p.command[*]'
void replace_while_borrowed(Planned& p) {
  if (auto* run = std::get_if<RunTo>(&p.command)) {
    p.command = PassTo{};
    run->player = 1;
  }
}

// Returnerad pekare: se 19_nullable_return.cpp.
// rules: STN-11, STN-39
// expect: stone: assuming
const PassTo* leak(const Planned& p) { return std::get_if<PassTo>(&p.command); }

// En pekare får flyttas till ett annat objekt: lånemodellen följer det nya lånet.
// rules: STN-39, STN-62
// expect: stone: assuming
// loans: ok
void reseat(const Planned& p, const Planned& q) {
  const auto* pass = std::get_if<PassTo>(&p.command);
  pass = std::get_if<PassTo>(&q.command);
}

// --- lambdor ----------------------------------------------------------------

// rules: STN-40
// expect: stone: assuming
int player_of(const Command& c) {
  return std::visit([](const auto& cmd) { return cmd.player; }, c);
}

// rules: STN-1
// expect: stone
int add(int a, int b) { return a + b; }

// rules: STN-40
// expect: stone
float scaled(float k, Vec2 v) {
  auto f = [&](float x) { return x * k; };
  return f(v.x) + f(v.y);
}

// rules: STN-40
// expect: stone
int count_up(int n) {
  int total = 0;
  auto bump = [&](int by) { total = add(total, by); };
  bump(n);
  bump(1);
  return total;
}

// rules: STN-40, STN-21
// expect: not-stone: side effect inside expression
int bump_inside(int n) {
  int total = 0;
  auto bump = [&](int by) { total = add(total, by); return total; };
  return bump(n) + bump(1);
}

// rules: STN-40, STN-62
// expect: not-stone: may be the same object
void overlap(Vec2& v) {
  auto set_x = [&](Vec2& other) { v.x = other.y; other.y = 0.0F; };
  set_x(v);
}

// rules: STN-40
// expect: stone: assuming
void apply_all(World& w, const Command& c) {
  std::visit([&w](const auto& cmd) { w.passes = add(w.passes, cmd.player); }, c);
}

struct Counter {
  int n{0};
  // rules: STN-40
  // expect: not-stone: captures 'this'
  int twice() { auto f = [this] { return n + n; }; return f(); }
};

// rules: STN-40
// expect: not-stone: lambda 'f' can only be called
int escape(int a) {
  auto f = [](int x) { return x; };
  auto g = f;
  return g(a);
}
