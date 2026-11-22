// pork-args: --silver --vector-bound 4
//
// Silver för åtkomstformerna i 15_access.cpp:
//  - skrivning genom v[i] / *o / o->f blir en skrivning till elementet i
//    modellen (lvalue (*(kontroll, &o.data[i]))),
//  - o->f kräver has_value() som *o,
//  - range-for över en vektor blir en indexloop upp till size,
//  - (void)f(x) och implicit standardkonstruktor (fältinitierarna).

#include <pork/annotations.hpp>

#include <optional>
#include <vector>

struct Vec2 { float x{0.0F}; float y{0.0F}; };
struct Flight { Vec2 target; int receiver{0}; };
struct Ball { std::optional<int> carrier; std::optional<Flight> flight; };
struct Player { Vec2 position; float speed{8.0F}; };
struct World { std::vector<Player> players; Ball ball; int phase{1}; };

// rules: SLV-14
// expect: stone: not silver: precondition of 'std::optional::operator->'
float target_any(const Ball& b) { return b.flight->target.x; }

// rules: SLV-14
// expect: silver: assuming
float target_x(const Ball& b) { return b.flight ? b.flight->target.x : 0.0F; }

// rules: SLV-14
// expect: silver: assuming
void retarget(Ball& b, Vec2 t) { if (b.flight) b.flight->target = t; }

// rules: SLV-13
// expect: stone: not silver: precondition of 'std::vector::operator[]'
void set_any(World& w, unsigned long i) { w.players[i].speed = 1.0F; }

// rules: SLV-13, SLV-17
// expect: silver: assuming std::vector size <= 4
void set_speed(World& w, unsigned long i, float s) { PORK_PRE(i < w.players.size()); w.players[i].speed = s; }

// rules: SLV-14
// expect: silver: assuming
void set_carrier(Ball& b, int id) { if (b.carrier) *b.carrier = id; }

// rules: SLV-23
// expect: silver: assuming
void copy_flight(Ball& to, const Ball& from) { to.flight = from.flight; }

// rules: SLV-9, SLV-24
// expect: stone: not silver: arithmetic overflow on floating-point addition
float sum_speed(const World& w) { float s = 0.0F; for (const Player& p : w.players) s += p.speed; return s; }

// rules: SLV-24, SLV-16
// expect: silver: assuming std::vector size <= 4
float max_speed(const World& w) {
  float m = 0.0F;
  for (const Player& p : w.players) m = p.speed > m ? p.speed : m;
  return m;
}

// rules: SLV-24, SLV-16
// expect: silver: assuming std::vector size <= 4
void stop_all(World& w) { for (Player& p : w.players) p.speed = 0.0F; }

// rules: SLV-5
// expect: stone: not silver: arithmetic overflow on signed +
int count_fast(const World& w, int n) { for (Player p : w.players) if (p.speed > 1.0F) n += 1; return n; }

// rules: SLV-17
// expect: silver
int step_once(int& x) { PORK_PRE(x < 100); x = x + 1; return x; }

// rules: SLV-17
// expect: silver
void ignore_result(int& x) { PORK_PRE(x < 50); (void)step_once(x); }

// rules: SLV-2
// expect: silver: assuming
World fresh() { World w; w.phase = 2; return w; }

// rules: SLV-2, SLV-23
// expect: silver: assuming
int fresh_phase() { const World w = fresh(); return w.phase + w.ball.flight.has_value(); }

// std::vector<bool>: icke-const operator[] ger proxyn std::_Bit_reference.
// Läsning (operator bool) och skrivning (operator=) behandlas som elementet.
// rules: SLV-13, SLV-17
// expect: silver: assuming
bool flag_at(std::vector<bool>& done, unsigned long i) { PORK_PRE(i < done.size()); return done[i]; }

// rules: SLV-13, SLV-17
// expect: silver: assuming
void set_flag(std::vector<bool>& done, unsigned long i) { PORK_PRE(i < done.size()); done[i] = true; }

// rules: SLV-13
// expect: stone: not silver: precondition of 'std::vector::operator[]'
void set_flag_any(std::vector<bool>& done, unsigned long i) { done[i] = true; }

// rules: SLV-24
// expect: silver: assuming
int count_done(std::vector<bool>& done) { int n = 0; for (unsigned long i = 0; i < done.size(); ++i) if (done[i] || false) n = n + 1; return n; }

// Eftervillkoret kontrollerar att skrivningen når elementet.
// rules: SLV-24, SLV-18
// expect: silver: assuming
bool set_then_get(unsigned long n) { PORK_PRE(n > 0 && n <= 4); PORK_POST(bool r, r); std::vector<bool> v{}; v.assign(n, false); v[n - 1] = true; return v[n - 1]; }
