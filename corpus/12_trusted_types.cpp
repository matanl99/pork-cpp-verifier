// Betrodda typer (12_trusted_types.trust): `type std::optional` gör
// std::optional<T> till en värdetyp när T är en värdetyp. Medlemmarna
// betros via `std::optional::*`. Allt redovisas som antaganden.

#include <optional>

struct Vec2 { int x; int y; };
struct Player { int id; std::optional<Vec2> target; };
struct Holder { int* p; };

// rules: STN-4
// expect: stone: assuming std::optional::optional, type std::optional
std::optional<int> none() { return std::nullopt; }

// rules: STN-4
// expect: stone: assuming type std::optional
std::optional<Vec2> target_of(const Player& p) { return p.target; }

// rules: STN-4, STN-37
// expect: stone: assuming std::optional::has_value, std::optional::operator*, type std::optional
int x_or(const Player& p, int fallback) { if (p.target.has_value()) return (*p.target).x; return fallback; }

// rules: STN-4
// expect: stone: assuming std::optional::optional, type std::optional
std::optional<int> some(int a) { return a; }

// rules: STN-4, STN-25
// expect: stone: assuming std::optional::value_or, type std::optional
int value_or_zero(std::optional<int> o) { return o.value_or(0); }

// rules: STN-4
// expect: stone: assuming std::optional::operator=, type std::optional
Player retarget(Player p, Vec2 v) { p.target = v; return p; }

// rules: STN-4
// expect: stone: assuming std::optional::reset, type std::optional
Player clear(Player p) { p.target.reset(); return p; }

// rules: STN-4
// expect: not-stone: template argument Holder of std::optional<Holder> is not a value type
int bad(std::optional<Holder> h) { return 0; }

// Strukturregeln: en struct med betrodda fält är en värdetyp även om den
// inte är trivialt kopierbar; egna kopierings-/flyttfunktioner är
// funktioner som granskas där de körs (STN-72).
struct OwnCopy { std::optional<int> v; OwnCopy(const OwnCopy&); };

// rules: STN-2, STN-72
// expect: stone: assuming type std::optional
int own_copy(const OwnCopy& o) { return 0; }

// Icke-const *o som bara läses räknas som const: std::optional har en
// const-tvilling, och överlagringen väljs bara för att o inte är const.
// rules: STN-37
// expect: stone: assuming
int read_local(int a) { std::optional<int> o = a; return *o + 1; }

// rules: STN-37
// expect: stone: assuming
int read_param(std::optional<int>& o) { return o ? *o * 2 : 0; }

// Skrivning genom *o som egen sats: en skrivning till o (se 15_access.cpp).
// rules: STN-37
// expect: stone: assuming
void write_through(std::optional<int>& o) { *o = 1; }

// Inuti ett uttryck är den fortfarande en sidoeffekt.
// rules: STN-37, STN-20
// expect: not-stone: side effect inside expression
int write_inside(std::optional<int>& o) { return (*o = 1) + 1; }
