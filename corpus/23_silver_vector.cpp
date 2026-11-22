// pork-args: --silver --vector-bound 8
//
// std::vector<T> i Silver: en begränsad modell { size, data[N] } med
// N = --vector-bound. Beviset gäller vektorer med högst N element; det
// redovisas som antagandet "std::vector size <= N".
//  - v[i], front(), back() kräver giltigt index / icke-tom vektor.
//  - push_back kräver att vektorn inte växer förbi N (modellens gräns).
//  - Lokala referenser till element blir pekare; Stones lån garanterar att
//    vektorn inte ändras medan referensen lever.

#include <pork/annotations.hpp>

#include <optional>
#include <vector>

struct Vec2 { float x; float y; };
struct Player { Vec2 position; std::optional<Vec2> target; float speed; };
struct Team { std::vector<Player> players; int score; };

// rules: SLV-24, SLV-22
// expect: silver: assuming std::vector size <= 8
unsigned long count(const std::vector<int>& v) { return v.size(); }

// rules: SLV-13
// expect: stone: not silver: precondition of 'std::vector::operator[]'
int at_any(const std::vector<int>& v, unsigned long i) { return v[i]; }

// rules: SLV-13, SLV-17
// expect: silver: assuming std::vector size <= 8
int at_checked(const std::vector<int>& v, unsigned long i) { PORK_PRE(i < v.size()); return v[i]; }

// rules: SLV-13
// expect: stone: not silver: precondition of 'std::vector::front'
int first(const std::vector<int>& v) { return v.front(); }

// rules: SLV-13
// expect: silver: assuming
int first_or(const std::vector<int>& v, int d) { return v.empty() ? d : v.front(); }

// rules: SLV-13, SLV-17
// expect: silver: assuming
int last_checked(const std::vector<int>& v) { PORK_PRE(!v.empty()); return v.back(); }

// Summering: elementen kan vara vad som helst, så summan kan svämma över.
// rules: SLV-5, SLV-24
// expect: stone: not silver: arithmetic overflow on signed +
int sum(const std::vector<int>& v) { int s = 0; for (unsigned long i = 0; i < v.size(); ++i) s = s + v[i]; return s; }

// rules: SLV-24, SLV-16
// expect: silver: assuming
int count_positive(const std::vector<int>& v) { int n = 0; for (unsigned long i = 0; i < v.size(); ++i) if (v[i] > 0) n = n + 1; return n; }

// rules: SLV-24
// expect: stone: not silver: std::vector may grow beyond 8 elements
void add(std::vector<int>& v, int x) { v.push_back(x); }

// rules: SLV-24, SLV-17
// expect: silver: assuming
void add_checked(std::vector<int>& v, int x) { PORK_PRE(v.size() < 8); v.push_back(x); }

// rules: SLV-24
// expect: silver: assuming
int push_then_read(int x) { std::vector<int> v{}; v.push_back(x); v.push_back(x); return v[1]; }

// rules: SLV-24
// expect: silver: assuming
void clear_all(std::vector<int>& v) { v.clear(); }

// assign(n, x): storleken blir n och alla element x. n måste rymmas i modellen.
// rules: SLV-24
// expect: stone: not silver: std::vector may grow beyond 8 elements
void fill_any(std::vector<int>& v, unsigned long n, int x) { v.assign(n, x); }

// Eftervillkoret kontrollerar att elementen faktiskt fylls.
// rules: SLV-24, SLV-18
// expect: silver: assuming
int fill_then_read(unsigned long n, int x) { PORK_PRE(n > 0 && n <= 8); PORK_POST(int r, r == x); std::vector<int> v{}; v.assign(n, x); return v[n - 1]; }

// Som rugbys TurnController::commit: done_.assign(planned_.size(), false).
// rules: SLV-24
// expect: silver: assuming
void reset_flags(std::vector<bool>& done, const std::vector<int>& planned) { done.assign(planned.size(), false); }

// rules: SLV-24
// expect: silver: assuming
bool valid(const Team& t, unsigned long id) { return id < t.players.size(); }

// Läsning via icke-const operator[] (const-tvilling) och element-referenser.
// rules: SLV-13, SLV-17
// expect: silver: assuming
float x_of(Team& t, unsigned long id) { PORK_PRE(valid(t, id)); return t.players[id].position.x; }

// rules: SLV-13, SLV-17
// expect: silver: assuming
void stop(Team& t, unsigned long id) { PORK_PRE(id < t.players.size()); Player& p = t.players[id]; p.speed = 0.0F; p.target = std::nullopt; }

// rules: SLV-13
// expect: stone: not silver: precondition of 'std::vector::operator[]'
void stop_any(Team& t, unsigned long id) { Player& p = t.players[id]; p.speed = 0.0F; }

// rules: SLV-24, SLV-16
// expect: silver: assuming
int has_targets(const Team& t) { int n = 0; for (unsigned long i = 0; i < t.players.size(); ++i) { const Player& p = t.players[i]; if (p.target) n = n + 1; } return n; }

// rules: SLV-24
// expect: silver: assuming
std::vector<int> copy_of(const std::vector<int>& v) { std::vector<int> w = v; return w; }

// resize(n) värdeinitierar nya element (fältinitierare, annars noll),
// resize(n, x) fyller med x. Krymper: bara size ändras.
struct Sample { float speed{8.0F}; int hits; };

// rules: SLV-24, SLV-18
// expect: silver: assuming
float grown(unsigned long n) { PORK_PRE(n > 0 && n <= 8); PORK_POST(float r, r == 8.0F); std::vector<Sample> v{}; v.resize(n); return v[n - 1].speed + float(v[n - 1].hits); }

// rules: SLV-24, SLV-18
// expect: silver: assuming
int filled(unsigned long n) { PORK_PRE(n >= 2 && n <= 8); PORK_POST(int r, r == 5); std::vector<int> v{}; v.push_back(5); v.resize(n, 9); return v[0] + v[n - 1] - 9; }

// rules: SLV-24, SLV-18
// expect: silver: assuming
unsigned long shrunk(std::vector<int>& v) { PORK_PRE(v.size() >= 2); PORK_POST(unsigned long r, r == 1); v.resize(1); return v.size(); }

// rules: SLV-24
// expect: stone: not silver: std::vector may grow beyond 8 elements
void resize_any(std::vector<int>& v, unsigned long n) { v.resize(n); }

// rules: SLV-24
// expect: silver: assuming
unsigned long nested(unsigned long n) { PORK_PRE(n <= 8); std::vector<std::vector<Vec2>> paths{}; paths.resize(n); if (n > 0) paths[0].push_back(Vec2{1.0F, 2.0F}); return paths.size(); }
