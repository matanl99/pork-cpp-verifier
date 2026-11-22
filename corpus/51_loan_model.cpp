// Lånemodellen (STN-62, docs/stone_model.md): osäkra varianter av dagens
// specialfall och fall där modellen och reglerna kommer fram till samma sak
// av olika skäl. `// loans:` är modellens dom (tests/check_loans.py).

#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <variant>
#include <vector>

struct P { int x; };
struct Box {
  std::vector<int> items;
  // rules: STN-62
  // expect: stone: assuming
  // loans: ok
  void add(int x) { items.push_back(x); }
};

// rules: STN-62
// expect: stone: assuming
// loans: ok
void grow(std::vector<int>& v) { v.push_back(1); }

// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'v' while 'p' borrows 'v[*]'
int getif_after_reassign(std::variant<int, double>& v) {
  int* p = std::get_if<int>(&v);
  v = 2.0;
  return p ? *p : 0;
}

// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'm' while 'r' borrows 'm[*]'
int at_after_clear(std::map<int, int>& m, int k) {
  const int& r = m.at(k);
  m.clear();
  return r;
}

// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'm' while 'it' borrows 'm[*]'
int find_after_clear(std::map<int, int>& m, int k) {
  auto it = m.find(k);
  m.clear();
  return it != m.end() ? it->second : 0;
}

// Ändringen sker i en egen funktion: clangs -Wlifetime-safety missar den.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'v' while 'r' borrows 'v[*]'
int via_own_function(std::vector<int>& v) {
  int& r = v[0];
  grow(v);
  return r;
}

// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'b.items' while 'r' borrows 'b.items[*]'
int via_method(Box& b) {
  int& r = b.items[0];
  b.add(2);
  return r;
}

// set::insert ogiltigförklarar inga iteratorer ([associative.reqmts.general],
// summaries/std.txt): minnessäkert. Att loopen kan se de nya elementen är
// terminering (Silver), inte Stone. Ingen körning: loopen tar aldrig slut.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void insert_while_iterating(std::set<int>& s) {
  for (int k : s) s.insert(k + 1);
}

// Lånet dör före ändringen: godkänt av båda.
// rules: STN-62
// expect: stone: assuming
// loans: ok
int dead_before_change(std::vector<int>& v) {
  int& r = v[0];
  int x = r;
  v.push_back(x);
  return x;
}

// Iteratorn kopieras: samma lån, ingen ny regel behövs.
// rules: STN-62
// expect: stone: assuming
// loans: ok
int copied_iterator(const std::set<int>& s, int key) {
  auto it = s.find(key);
  auto copy = it;
  return copy == s.end() ? 0 : *copy;
}

// rules: STN-62
// expect: not-stone
// loans: rejected: not known to be the same object
bool wrong_end(const std::set<int>& a, const std::set<int>& b, int key) {
  auto it = a.find(key);
  return it == b.end();
}

// En lambda som fångar v och får v som argument: G3.
// rules: STN-62
// expect: not-stone
// loans: rejected: may be the same object and one is changed
void lambda_alias(P& v) {
  auto set_x = [&](P& other) { v.x = other.x + 1; other.x = 0; };
  set_x(v);
}

// Tillåtelselista: ett element utan känd låneeffekt är ett nej (asm kan skriva vad som helst).
// rules: STN-62
// expect: not-stone
// loans: rejected: cannot follow the loans of GCCAsmStmt
int with_asm(std::vector<int>& v) {
  int& r = v[0];
  asm volatile("" ::: "memory");
  return r;
}

// En statisk lokal variabel överlever funktionen.
// rules: STN-62
// expect: not-stone
// loans: rejected: stores a loan of 'x'
const int* keep_static(int x) {
  static const int* p = &x;
  return p;
}

// rules: STN-62
// expect: stone
// loans: ok
int plain(int a, int b) { return a < b ? -a : b + static_cast<int>(sizeof(a)); }

// --- Sammanfattningar av egna funktioner ---
// En egen funktion sammanfattas ur sin kropp: vad den ändrar (relativt
// parametrarna och *this) och vad returen lånar. Anroparen använder det i
// stället för "kan ändra allt den får en referens till".

struct Squad {
  std::vector<P> players;
  P wind;
};
// rules: STN-62
// expect: stone: assuming
// loans: ok
void calm(Squad& s) { s.wind = P{0}; }
// rules: STN-62
// expect: stone: assuming
// loans: ok
void clear_squad(Squad& s) { s.players.clear(); }
// rules: STN-62
// expect: stone: assuming
// loans: ok
void calm_then_clear(Squad& s) {
  calm(s);
  clear_squad(s);
}

// calm ändrar bara s.wind: p in i s.players är giltig.
// rules: STN-62
// expect: stone: assuming
// loans: ok
int keep_player(Squad& s) {
  P& p = s.players[0];
  calm(s);
  return p.x;
}

// clear_squad ändrar s.players; p används efter grenen.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 's.players' while 'p' borrows 's.players[*]'
int clear_in_branch(Squad& s, bool b) {
  P& p = s.players[0];
  if (b) clear_squad(s);
  return p.x;
}

// Transitivt: sammanfattningen av calm_then_clear innehåller clear_squads ändring.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 's.players' while 'p' borrows 's.players[*]'
int via_two_levels(Squad& s) {
  P& p = s.players[0];
  calm_then_clear(s);
  return p.x;
}

// rules: STN-62
// expect: stone: assuming
// loans: ok
int dead_before_clear(Squad& s) {
  P& p = s.players[0];
  int x = p.x;
  clear_squad(s);
  return x;
}

// Rekursion: ingen sammanfattning, anropet får den försiktiga standarden.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void countdown(Squad& s, int n) {
  if (n > 0)
    countdown(s, n - 1);
  else
    calm(s);
}
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 's
int recursive(Squad& s) {
  P& p = s.players[0];
  countdown(s, 3);
  return p.x;
}

struct Roster {
  std::vector<P> players;
  std::vector<int> scores;
  // rules: STN-62
  // expect: stone: assuming
  // loans: ok
  const std::vector<P>& all() const { return players; }
  // rules: STN-62
  // expect: stone: assuming
  // loans: ok
  void score(int v) { scores.push_back(v); }
  // rules: STN-62
  // expect: stone: assuming
  // loans: ok
  void sign(P p) { players.push_back(p); }
};

// En getter lånar exakt fältet den returnerar (reglerna följer inte anropet).
// rules: STN-62
// expect: stone: assuming
// loans: ok
int getter_then_other(Roster& r) {
  const P& first = r.all()[0];
  r.score(1);
  return first.x;
}
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'r.players' while 'first' borrows 'r.players[*]'
int getter_then_same(Roster& r) {
  const P& first = r.all()[0];
  r.sign(P{1});
  return first.x;
}

// G3 vid anrop gäller oavsett vilka fält som skrivs.
// rules: STN-62
// expect: stone: assuming
// loans: ok
void copy_wind(Squad& to, const Squad& from) { to.wind = from.wind; }
// rules: STN-62
// expect: not-stone
// loans: rejected: may be the same object
void self_copy(Squad& s) { copy_wind(s, s); }

// Ändring genom ett pekarfält: kan inte uttryckas i sammanfattningen
// (h.list kan peka på v), så anropet får standarden.
struct Holder {
  std::vector<P>* list;
  // rules: STN-62
  // expect: not-stone
  // loans: ok
  void grow() { list->push_back(P{0}); }
};
// rules: STN-62
// expect: not-stone
// loans: rejected
int via_pointer_member(Holder& h, std::vector<P>& v) {
  P& p = v[0];
  h.grow();
  return p.x;
}

// Utan anrop: det ett pekarfält i en parameter pekar på kan vara en annan
// parameter (inte lagring som h äger).
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'list' while 'p' borrows 'v[*]'
int pointer_member_direct(Holder& h, std::vector<P>& v) {
  P& p = v[0];
  h.list->push_back(P{0});
  return p.x;
}

// --- Std-sammanfattningar som data (summaries/std.txt) ---

// Nodbaserade behållare: insättning flyttar inga element.
// rules: STN-62
// expect: not-stone
// loans: ok
int map_insert_keeps(std::map<int, int>& m) {
  const int& r = m.at(1);
  m.insert({2, 2});
  return r;
}
// rules: STN-62
// expect: stone: assuming
// loans: ok
int list_push_keeps(std::list<int>& l) {
  int& r = l.front();
  l.push_back(1);
  return r;
}
// Omallokering och rehash: ingen regel, standarden ogiltigförklarar.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'v' while 'r' borrows 'v[*]'
int vector_push_breaks(std::vector<int>& v) {
  int& r = v.front();
  v.push_back(1);
  return r;
}
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'm' while 'it' borrows 'm[*]'
int unordered_insert_breaks(std::unordered_map<int, int>& m) {
  auto it = m.find(1);
  m.insert({2, 2});
  return it == m.end() ? 0 : it->second;
}

// `consumes`: iteratorn dör men skrivs över av resultatet.
// rules: STN-62, STN-74
// expect: stone: assuming
// loans: ok
void erase_loop(std::map<int, int>& m) {
  for (auto it = m.begin(); it != m.end();) {
    if (it->second < 0)
      it = m.erase(it);
    else
      ++it;
  }
}
// rules: STN-62, STN-74
// expect: stone: assuming
// loans: ok
void erase_loop_vector(std::vector<int>& v) {
  for (auto it = v.begin(); it != v.end();) {
    if (*it < 0)
      it = v.erase(it);
    else
      ++it;
  }
}
// Utan tilldelning tillbaka: iteratorn används efter raderingen.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'm' while 'it' borrows 'm[*]'
int erase_then_use(std::map<int, int>& m) {
  auto it = m.begin();
  m.erase(it);
  return it->second;
}
// En annan iterator kan peka på det raderade elementet.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'm' while 'a' borrows 'm[*]'
int erase_other_alive(std::map<int, int>& m) {
  auto a = m.begin();
  auto b = m.begin();
  b = m.erase(b);
  return a->second;
}
// Tilldelning till en annan variabel: inte `consumes`.
// rules: STN-62
// expect: not-stone
// loans: rejected: changes 'm' while 'it' borrows 'm[*]'
int erase_into_other(std::map<int, int>& m) {
  auto it = m.begin();
  auto next = m.erase(it);
  return it->second + (next == m.end() ? 0 : 1);
}

// `position`: insättningspositionen pekar in i behållaren per definition.
// rules: STN-62, STN-74
// expect: stone: assuming
// loans: ok
void insert_at_front(std::vector<int>& v) { v.insert(v.begin(), 1); }
// Men i insert(p, i, j) får i och j inte peka in i behållaren (G3).
// rules: STN-62
// expect: not-stone
// loans: rejected: may be the same object
void insert_own_range(std::vector<int>& v) { v.insert(v.end(), v.begin(), v.end()); }

#ifdef PORK_RUN
#include <pork/run.hpp>
using IntMap = std::map<int, int>;
PORK_RUN_CASES(
    // Lån som lånemodellen avvisar och som körningen kan visa.
    {"getif_after_reassign", [] { std::variant<int, double> v = 1; return getif_after_reassign(v) == 1 ? 0 : 1; }},
    {"at_after_clear", [] { IntMap m{{1, 1}}; return at_after_clear(m, 1); }},
    {"find_after_clear", [] { IntMap m{{1, 1}}; return find_after_clear(m, 1); }},
    {"via_own_function", [] { std::vector<int> v{1}; return via_own_function(v) == 1 ? 0 : 1; }},
    {"via_method", [] { Box b; b.add(1); return via_method(b) == 1 ? 0 : 1; }},
    {"keep_player", [] { Squad s{{{1}}, {0}}; return keep_player(s) == 1 ? 0 : 1; }},
    {"getter_then_other", [] { Roster r{{P{4}}, {}}; return getter_then_other(r) == 4 ? 0 : 1; }},
    {"list_push_keeps", [] { std::list<int> l{9}; return list_push_keeps(l) == 9 ? 0 : 1; }},
    {"via_two_levels", [] { Squad s{{{1}}, {0}}; return via_two_levels(s) == 1 ? 0 : 1; }},
    {"getter_then_same", [] { Roster r; r.sign({1}); return getter_then_same(r) == 1 ? 0 : 1; }},
    {"vector_push_breaks", [] { std::vector<int> v{1}; return vector_push_breaks(v) == 1 ? 0 : 1; }},
    {"erase_then_use", [] { IntMap m{{1, 1}, {2, 2}}; return erase_then_use(m) == 1 ? 0 : 1; }},
    {"insert_own_range", [] { std::vector<int> v{1, 2}; insert_own_range(v); return v.size() == 4 ? 0 : 1; }},
    // Stone: tyst och rätt värde.
    {"Box::add", [] { Box b; b.add(2); return b.items.size() == 1 ? 0 : 1; }},
    {"grow", [] { std::vector<int> v; grow(v); return v.size() == 1 ? 0 : 1; }},
    {"dead_before_change", [] { std::vector<int> v{4}; return dead_before_change(v) == 4 && v.size() == 2 ? 0 : 1; }},
    {"copied_iterator", [] { std::set<int> s{1, 5}; return copied_iterator(s, 5) == 5 && copied_iterator(s, 2) == 0 ? 0 : 1; }},
    {"plain", [] { return plain(1, 2) == -1 ? 0 : 1; }},
    {"calm", [] { Squad s{{}, {3}}; calm(s); return s.wind.x; }},
    {"clear_squad", [] { Squad s{{{1}}, {0}}; clear_squad(s); return s.players.empty() ? 0 : 1; }},
    {"calm_then_clear", [] { Squad s{{{1}}, {3}}; calm_then_clear(s); return s.players.empty() && s.wind.x == 0 ? 0 : 1; }},
    {"dead_before_clear", [] { Squad s{{{6}}, {0}}; return dead_before_clear(s) == 6 ? 0 : 1; }},
    {"countdown", [] { Squad s{{}, {3}}; countdown(s, 3); return s.wind.x; }},
    {"Roster::all", [] { Roster r; r.sign({1}); return r.all().size() == 1 ? 0 : 1; }},
    {"Roster::score", [] { Roster r; r.score(2); return r.scores.size() == 1 ? 0 : 1; }},
    {"Roster::sign", [] { Roster r; r.sign({2}); return r.players.size() == 1 ? 0 : 1; }},
    {"copy_wind", [] { Squad a{{}, {0}}, b{{}, {4}}; copy_wind(a, b); return a.wind.x == 4 ? 0 : 1; }},
    {"erase_loop", [] { IntMap m{{1, -1}, {2, 2}, {3, -3}}; erase_loop(m); return m.size() == 1 ? 0 : 1; }},
    {"erase_loop_vector", [] { std::vector<int> v{-1, 2, -3}; erase_loop_vector(v); return v.size() == 1 ? 0 : 1; }},
    {"insert_at_front", [] { std::vector<int> v{2}; insert_at_front(v); return v[0] == 1 ? 0 : 1; }})
#endif
