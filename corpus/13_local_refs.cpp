// Lokala referenser.
//  - Bunden till en väg (`const Ball& b = w.ball`): ett annat namn för vägen.
//    Aliasing-kontrollen ser igenom referensen.
//  - Bunden till en temporär (`const Vec2& v = Vec2{...}`): livslängden förlängs,
//    referensen är som en lokal variabel.
//  - Bunden till ett element (`Vec2& p = t.players[i]`): ett lån av behållaren.
//    Om en ändring dödar lånet avgör lånemodellen (STN-62,
//    ensam): en skrivning i ett fält eller ett annat element gör det inte,
//    en omallokering gör det. Lånet slutar vid sista användningen.

#include <vector>

struct Vec2 { int x; int y; };
struct Ball { Vec2 pos; int carrier; };
struct World { Ball ball; Vec2 wind; };
struct Team { std::vector<Vec2> players; Vec2 wind; };

// rules: STN-12, STN-26
// expect: stone
void move_by(Vec2& p, const Vec2& d) { p.x += d.x; p.y += d.y; }

// rules: STN-10
// expect: stone
const int& id_ref(const int& x) { return x; }

// --- Bundna till en väg eller en temporär ---

// rules: STN-31
// expect: stone
int read_path(const World& w) { const Ball& b = w.ball; return b.pos.x + b.carrier; }

// rules: STN-31
// expect: stone
void write_path(World& w) { Ball& b = w.ball; b.pos.x += 1; b.carrier = 0; }

// rules: STN-31
// expect: stone
int nested(World w) { Ball& b = w.ball; Vec2& p = b.pos; p.y = 2; return w.ball.pos.y; }

// rules: STN-32
// expect: stone
int temporary(int a) { const Vec2& v = Vec2{a, a}; return v.x; }

// rules: STN-31, STN-62
// expect: stone
void through_ref(World& w) { Ball& b = w.ball; move_by(b.pos, w.wind); }

// rules: STN-31, STN-62
// expect: not-stone: may be the same object
void alias_via_ref(World& w) { Ball& b = w.ball; move_by(b.pos, w.ball.pos); }

// rules: STN-31, STN-62
// expect: not-stone: may be the same object
void alias_two_refs(World& w) { Ball& b = w.ball; Vec2& p = w.ball.pos; move_by(p, b.pos); }

// rules: STN-36
// expect: stone
int conditional(Vec2 a, Vec2 b, bool c) { const Vec2& r = c ? a : b; return r.x; }

// rules: STN-36
// expect: not-stone: borrows a temporary
int dangling(int a) { const int& r = id_ref(5); return a + r; }

// rules: STN-36
// expect: not-stone: local variable 'r' has type Vec2 &&
int rvalue_local(Vec2 v) { Vec2&& r = Vec2{1, 2}; return r.x + v.x; }

// --- Bundna till ett element (lån) ---

// rules: STN-4
// expect: stone: assuming type std::allocator, type std::vector
void reset_team(Team& t) { t.wind = Vec2{}; }

// rules: STN-62, STN-37
// expect: stone: assuming
int read_element(const Team& t, unsigned long i) { const Vec2& p = t.players[i]; return p.x; }

// rules: STN-62
// expect: stone: assuming
void write_element(Team& t, unsigned long i) { Vec2& p = t.players[i]; p.x += t.wind.x; }

// rules: STN-62, STN-26
// expect: stone: assuming
void element_by_ref(Team& t, unsigned long i) { Vec2& p = t.players[i]; move_by(p, t.wind); }

// rules: STN-62
// expect: stone: assuming
int const_two_elements(const Team& t) { const Vec2& p = t.players[0]; const Vec2& q = t.players[1]; return p.x + q.x; }

// rules: STN-62
// expect: stone: assuming
void borrow_ends(Team& t) { { Vec2& p = t.players[0]; p.x = 1; } t.players.push_back(Vec2{}); }

// rules: STN-62
// expect: not-stone: changes 't.players' while 'p' borrows
void push_while_borrowed(Team& t) { Vec2& p = t.players[0]; t.players.push_back(Vec2{}); p.x = 1; }

// rules: STN-62
// expect: stone: assuming
// loans: ok
void second_element(Team& t) { Vec2& p = t.players[0]; Vec2& q = t.players[1]; p.x = q.x; }

// rules: STN-62
// expect: stone: assuming
// loans: ok
void pass_whole(Team& t) { Vec2& p = t.players[0]; reset_team(t); p.x = 1; }

// rules: STN-62
// expect: not-stone: changes 't.players' while 'p' borrows
void assign_whole(Team& t, const Team& other) { const Vec2& p = t.players[0]; t = other; t.wind.x = p.x; }

// rules: STN-12
// expect: stone: assuming type std::allocator, type std::vector
void touch(Vec2& p, const Team& t) { p.x = t.wind.x; }

// rules: STN-62
// expect: not-stone: may be the same object
void alias_element(Team& t, unsigned long i) { Vec2& p = t.players[i]; touch(p, t); }

// --- Lånet slutar vid sista användningen (som Rusts NLL) ---
// (`// loans: ok` nedan: lånemodellens sammanfattning av reset_team visar att
// bara t.wind ändras, så referenser in i t.players är giltiga. Modellens
// livslängdsfall med en funktion som verkligen ogiltigförklarar ligger i
// 51_loan_model.cpp.)
// En referens som inte används i någon senare sats (i blocket eller de
// omgivande blocken; i en loop hela loopen) håller inte längre sitt lån.

// rules: STN-62
// expect: stone: assuming
void dead_borrow(Team& t, unsigned long i) { Vec2& p = t.players[i]; p.x = 1; reset_team(t); }

// rules: STN-62
// expect: stone: assuming
void dead_borrow_push(Team& t) { Vec2& p = t.players[0]; p.x = 1; t.players.push_back(Vec2{}); }

// rules: STN-62
// expect: stone: assuming
void dead_in_branch(Team& t, bool b) { Vec2& p = t.players[0]; p.x = 1; if (b) reset_team(t); }

// Används efter ändringen i ett yttre block.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void used_after_branch(Team& t, bool b) { Vec2& p = t.players[0]; if (b) { reset_team(t); } p.x = 1; }

// Används genom ett alias.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void used_via_alias(Team& t) { Vec2& p = t.players[0]; Vec2& q = p; reset_team(t); q.x = 1; }

// Används tidigare i loopkroppen: nästa varv läser p efter ändringen.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void used_next_iteration(Team& t, unsigned long n) { Vec2& p = t.players[0]; for (unsigned long i = 0; i < n; ++i) { p.x += 1; reset_team(t); } }

// Rugbys move_players: efter ändringen kommer `continue`, och nästa varv
// binder en ny referens. Kontrollflödet avgör, inte textens ordning.
// rules: STN-62
// expect: stone: assuming
void continue_after(Team& t, unsigned long n) {
  for (unsigned long i = 0; i < n; ++i) {
    Vec2& p = t.players[i];
    if (p.x > 0) {
      reset_team(t);
      continue;
    }
    p.x = 1;
  }
}

// Utan continue används p efter ändringen.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void no_continue(Team& t, unsigned long n) {
  for (unsigned long i = 0; i < n; ++i) {
    Vec2& p = t.players[i];
    if (p.x > 0) reset_team(t);
    p.x = 1;
  }
}

// rules: STN-21
// expect: stone: assuming
bool reset_if_calm(Team& t) { const bool calm = t.wind.x == 0; if (calm) reset_team(t); return calm; }

// Villkoret ändrar t, grenen använder p efteråt.
// rules: STN-62, STN-21
// expect: stone: assuming
// loans: ok
void used_in_branch(Team& t) { Vec2& p = t.players[0]; if (reset_if_calm(t)) p.x = 1; }

// Range-for: iterationen använder behållaren, men reset_team ändrar bara t.wind.
// rules: STN-35
// expect: stone: assuming
// loans: ok
void range_for_modifies(Team& t) { for (Vec2& x : t.players) { x.x = 1; reset_team(t); } }

// Lånet lever bara ett varv: nästa varvs operator[] är tillåten.
// rules: STN-62
// expect: stone: assuming
void each_element(Team& t, unsigned long n) { for (unsigned long i = 0; i < n; ++i) { Vec2& p = t.players[i]; p.x += 1; } }

// Lånet deklarerat före loopen lever genom hela loopen.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void borrow_across_loop(Team& t, unsigned long n) { Vec2& first = t.players[0]; for (unsigned long i = 1; i < n; ++i) { Vec2& p = t.players[i]; first.x += p.x; } }

#ifdef PORK_RUN
#include <pork/run.hpp>
Team two() { return Team{{Vec2{1, 2}, Vec2{3, 4}}, Vec2{5, 6}}; }
PORK_RUN_CASES(
    {"conditional", [] { return conditional(Vec2{1, 0}, Vec2{2, 0}, false) == 2 ? 0 : 1; }},
    {"second_element", [] { Team t = two(); second_element(t); return t.players[0].x == 3 ? 0 : 1; }},
    {"pass_whole", [] { Team t = two(); pass_whole(t); return t.players[0].x == 1 && t.wind.x == 0 ? 0 : 1; }},
    {"used_after_branch", [] { Team t = two(); used_after_branch(t, true); return t.players[0].x == 1 ? 0 : 1; }},
    {"used_via_alias", [] { Team t = two(); used_via_alias(t); return t.players[0].x == 1 ? 0 : 1; }},
    {"used_next_iteration", [] { Team t = two(); used_next_iteration(t, 3); return t.players[0].x == 4 ? 0 : 1; }},
    {"no_continue", [] { Team t = two(); no_continue(t, 2); return t.players[1].x == 1 ? 0 : 1; }},
    {"used_in_branch", [] { Team t = two(); t.wind.x = 0; used_in_branch(t); return t.players[0].x == 1 ? 0 : 1; }},
    {"range_for_modifies", [] { Team t = two(); range_for_modifies(t); return t.players[1].x == 1 ? 0 : 1; }},
    {"borrow_across_loop", [] { Team t = two(); borrow_across_loop(t, 2); return t.players[0].x == 4 ? 0 : 1; }},
    {"push_while_borrowed", [] { Team t = two(); t.players.shrink_to_fit(); push_while_borrowed(t); return 0; }},
    {"assign_whole", [] { Team t = two(); t.players.shrink_to_fit(); Team o = two(); o.players.resize(64); assign_whole(t, o); return 0; }})
#endif
