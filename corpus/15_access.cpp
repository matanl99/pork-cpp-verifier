// Åtkomst via betrodda accessorer, (void), range-for och fältinitierare.
//
//  - opt->x är (*opt).x: operator-> i en systemheader räknas som en accessor,
//    inte som pekaråtkomst.
//  - Skrivning genom en accessor (v[i].f = x, *o = x, o->f = x) är en
//    skrivning till behållaren: objektet måste vara en väg, och levande lån
//    in i den underkänner skrivningen.
//  - En accessor vars resultat skickas som värde eller const& är en läsning.
//  - (void)f(...) är en sats med sidoeffekt, som f(...).
//  - range-for över en std::vector som är en väg: loopvariabeln lånar ett
//    element per varv, behållaren får inte ändras i loopen.
//  - T x; med implicit standardkonstruktor: fältinitierarna granskas; fält
//    utan initierare underkänns.

#include <optional>
#include <vector>

struct Vec2 { float x{0.0F}; float y{0.0F}; };
struct Flight { Vec2 target; int receiver{0}; };
struct Ball { std::optional<int> carrier; std::optional<Flight> flight; };
struct Player { Vec2 position; std::optional<Vec2> target; float speed{8.0F}; };
struct World { std::vector<Player> players; Ball ball; int phase{1}; };
struct Raw { int a; int b{2}; };

// rules: STN-2
// expect: stone
float vx(Vec2 v) { return v.x; }

// rules: STN-37
// expect: stone: assuming
float target_x(const Ball& b) { return b.flight ? b.flight->target.x : 0.0F; }

// rules: STN-37
// expect: stone: assuming
void retarget(Ball& b, Vec2 t) { if (b.flight) b.flight->target = t; }

// rules: STN-37
// expect: stone: assuming
void set_speed(World& w, unsigned long i, float s) { w.players[i].speed = s; }

// rules: STN-37
// expect: stone: assuming
void set_carrier(Ball& b, int id) { if (b.carrier) *b.carrier = id; }

// rules: STN-62, STN-37
// expect: stone: assuming
// loans: ok
void write_while_borrowed(World& w) { Player& p = w.players[0]; w.players[1].speed = 1.0F; p.speed = 2.0F; }

// rules: STN-37
// expect: stone: assuming
void copy_element(World& w, Ball& b) { b.flight = Flight{}; b.flight->target = w.players[0].position; }

// rules: STN-37
// expect: stone: assuming
float pass_by_value(Player& p) { return p.target ? vx(*p.target) : 0.0F; }

// rules: STN-12
// expect: stone
int step_once(int& x) { x = x + 1; return x; }

// rules: STN-21
// expect: stone
void ignore_result(int& x) { (void)step_once(x); }

// rules: STN-21
// expect: not-stone: side effect inside expression
int ignore_inside(int& x) { return (void)step_once(x), x; }

// rules: STN-35
// expect: stone: assuming
float sum_speed(const World& w) { float s = 0.0F; for (const Player& p : w.players) s += p.speed; return s; }

// rules: STN-35
// expect: stone: assuming
void stop_all(World& w) { for (Player& p : w.players) p.speed = 0.0F; }

// rules: STN-35
// expect: stone: assuming
int count_targets(const World& w) { int n = 0; for (Player p : w.players) if (p.target) n += 1; return n; }

// rules: STN-35
// expect: not-stone: may be the same object
void grow_in_loop(World& w) { for (Player& p : w.players) w.players.push_back(p); }

// rules: STN-35
// expect: not-stone: range-for over
int over_temporary(const World& w) { int n = 0; for (const Player& p : std::vector<Player>{w.players}) n += p.speed > 0.0F; return n; }

// rules: STN-15
// expect: stone: assuming
World fresh() { World w; w.phase = 2; return w; }

// rules: STN-15
// expect: stone
Vec2 origin() { Vec2 v; return v; }

// rules: STN-15
// expect: not-stone: field 'a' of Raw is not initialized
int raw() { Raw r; return r.b; }

// std::vector<bool>: proxyn från v[i] är elementet bara som läsning (till bool)
// och som skrivning `v[i] = <bool>`. Andra användningar känns inte igen.
// rules: STN-38
// expect: stone: assuming
bool flag(std::vector<bool>& v, unsigned long i) { return v[i]; }

// rules: STN-38
// expect: stone: assuming
void set_flag(std::vector<bool>& v, unsigned long i) { v[i] = true; }

// rules: STN-38
// expect: not-stone: local variable 'r' has type reference
bool keep_proxy(std::vector<bool>& v, unsigned long i) { auto r = v[i]; return r; }

// rules: STN-38
// expect: not-stone: side effect inside expression
void flip_flag(std::vector<bool>& v, unsigned long i) { v[i].flip(); }

// rules: STN-38
// expect: not-stone: expression of type _Bit_reference
void copy_flag(std::vector<bool>& v, unsigned long i, unsigned long j) { v[i] = v[j]; }

// rules: STN-38
// expect: not-stone: expression of type const std::_Bit_reference
bool set_in_expression(std::vector<bool>& v, unsigned long i) { return (v[i] = true); }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"write_while_borrowed", [] { World w; w.players.resize(2); write_while_borrowed(w); return w.players[0].speed == 2.0F && w.players[1].speed == 1.0F ? 0 : 1; }},
    {"grow_in_loop", [] { World w; w.players.resize(2); w.players.shrink_to_fit(); grow_in_loop(w); return 0; }})
#endif
