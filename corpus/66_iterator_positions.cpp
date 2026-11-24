// Iteratorer som positioner i insert/erase (STN-74). Förvillkoret i
// [sequence.reqmts]: positionen pekar in i behållaren, ett intervall är ett
// intervall i den. Fallen kommer från gruppexemplen (019 v_insert, 030
// v_erase) och från att lånemodellen tillät men inte krävde det: de tre
// "andra behållare"-fallen var Stone före STN-74 (_GLIBCXX_DEBUG: "attempt to
// insert into container with an iterator from a different container").
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>

struct Two {
  std::vector<int> a;
  std::vector<int> b;
};

// --- godtas -----------------------------------------------------------------

// rules: STN-74
// expect: stone: assuming
void append(std::vector<int>& v, int x) { v.insert(v.end(), x); }

// rules: STN-74
// expect: stone: assuming
void prepend(std::vector<int>& v, int x) { v.insert(v.begin(), x); }

// rules: STN-74
// expect: stone: assuming
void drop_first(std::vector<int>& v) {
  if (!v.empty()) v.erase(v.begin());
}

// Indexet kan ligga utanför: det är ett körtidsfel (Silver), inte ett lån.
// rules: STN-74
// expect: stone: assuming
void drop_at(std::vector<int>& v, int i) { v.erase(v.begin() + i); }

// rules: STN-74
// expect: stone: assuming
void drop_all(std::vector<int>& v) { v.erase(v.begin(), v.end()); }

// rules: STN-74, STN-58
// expect: stone: assuming
void insert_local(std::vector<int>& v) {
  auto it = v.end();
  v.insert(it, 1);
}

// Ett värde som refererar in i v: standarden kräver att det fungerar
// (std.txt `copies`, 112_copy_own_element.cpp).
// rules: STN-74, STN-62
// expect: stone: assuming
void duplicate_first(std::vector<int>& v) {
  if (!v.empty()) v.insert(v.end(), v[0]);
}

// rules: STN-74
// expect: stone: assuming
void append_other(std::vector<int>& v, const std::vector<int>& w) { v.insert(v.end(), w.begin(), w.end()); }

// rules: STN-74, STN-58
// expect: stone: assuming
void erase_loop(std::vector<int>& v) {
  for (auto it = v.begin(); it != v.end();) {
    if (*it < 0)
      it = v.erase(it);
    else
      ++it;
  }
}

// rules: STN-74
// expect: stone: assuming
void hint(std::map<int, int>& m) { m.insert(m.end(), {1, 2}); }

// --- avvisas: iteratorn kommer från en annan behållare ------------------------

// rules: STN-74, STN-58
// expect: not-stone: must point into
void insert_other(std::vector<int>& v, std::vector<int>& w) {
  auto it = w.end();
  v.insert(it, 1);
}

// rules: STN-74, STN-58
// expect: not-stone: must point into
void erase_other(std::vector<int>& v, std::vector<int>& w) {
  if (w.empty()) return;
  auto it = w.begin();
  v.erase(it);
}

// rules: STN-74, STN-58
// expect: not-stone: must point into
void erase_other_const(std::vector<int>& v, const std::vector<int>& w) {
  if (w.empty()) return;
  auto it = w.begin();
  v.erase(it);
}

// rules: STN-74, STN-58
// expect: not-stone: must point into
void erase_mixed_range(std::vector<int>& v, std::vector<int>& w) {
  auto b = v.begin();
  auto e = w.end();
  v.erase(b, e);
}

// rules: STN-74
// expect: not-stone: must point into
void insert_other_direct(std::vector<int>& v, std::vector<int>& w) { v.insert(w.end(), 1); }

// rules: STN-74
// expect: not-stone: must point into
void hint_other(std::map<int, int>& m, std::map<int, int>& n) { m.insert(n.end(), {1, 2}); }

// Kan vara vilken som helst av två: båda måste peka in i v.
// rules: STN-74, STN-58
// expect: not-stone: must point into
void maybe_other(std::vector<int>& v, std::vector<int>& w, bool c) {
  auto a = v.end();
  auto b = w.end();
  auto it = c ? a : b;
  v.insert(it, 1);
}

// rules: STN-74
// expect: not-stone: must point into
void other_field(Two& t) { t.a.insert(t.b.end(), 1); }

// rules: STN-74, STN-58
// expect: not-stone: must point into
void two_hops(std::vector<int>& v, std::vector<int>& w) {
  auto a = w.end();
  auto b = a;
  v.insert(b, 1);
}

// emplace_hint saknas i std.txt för unordered_map: kontrollen hänger på typen
// (hittat av check_summaries.py, POSITIONS).
// rules: STN-74, STN-58
// expect: not-stone: must point into
void hint_unordered_other(std::unordered_map<int, int>& m, std::unordered_map<int, int>& n) {
  auto it = n.end();
  m.emplace_hint(it, 1, 2);
}

// rules: STN-74
// expect: stone: assuming
void same_field(Two& t) { t.a.insert(t.a.end(), 1); }

// rules: STN-74
// expect: stone: assuming
void drop_last(std::vector<int>& v) {
  if (!v.empty()) v.erase(v.end() - 1);
}

struct Ctx {
  std::vector<int> items;
};

struct Inserter {
  Ctx& ctx;
  // Positionen kan peka in i ctx.items, men måste inte (STN-74).
  // rules: STN-74, STN-53
  // expect: not-stone: must point into
  void insert_other(std::vector<int>& w) {
    auto it = w.end();
    ctx.items.insert(it, 1);
  }
};

// --- avvisas: övrigt ------------------------------------------------------------

// Intervallet i insert(p, i, j) får inte peka in i v.
// rules: STN-74, STN-62
// expect: not-stone
void append_self(std::vector<int>& v) { v.insert(v.end(), v.begin(), v.end()); }

// En sparad iterator efter insert (STN-58, oförändrat).
// rules: STN-74, STN-58
// expect: not-stone: loans
int stale(std::vector<int>& v) {
  auto it = v.begin();
  v.insert(v.end(), 1);
  return *it;
}

// En iterator som skrivs genom: inte en läsning av v.
// rules: STN-74, STN-20
// expect: not-stone: side effect inside expression
void write_through(std::vector<int>& v) {
  if (!v.empty()) v.insert(v.end(), *v.begin() = 3);
}

// Jämförelsen med en iterator från en annan behållare (STN-62, oförändrat).
// rules: STN-74, STN-62
// expect: not-stone: which are not known to be the same object
bool compare_other(std::vector<int>& v, std::vector<int>& w) {
  auto it = v.begin();
  return it != w.end();
}

// Jämförelsen med ett villkorligt ursprung: kan vara olika objekt (STN-62).
// rules: STN-62, STN-58
// expect: not-stone: which are not known to be the same object
bool compare_conditional(std::vector<int>& v, std::vector<int>& w, bool c) {
  auto a = v.begin();
  auto b = w.begin();
  auto it = c ? a : b;
  return it == v.end();
}

// sort skriver genom iteratorerna: ingen position, men en sats (STN-78).
// rules: STN-74, STN-78
// expect: stone: assuming
void sort_all(std::vector<int>& v) { std::sort(v.begin(), v.end()); }

#ifdef PORK_RUN
#include <pork/run.hpp>
using Vec = std::vector<int>;
PORK_RUN_CASES(
    {"duplicate_first", [] { Vec v{1}; for (int i = 0; i < 20; ++i) duplicate_first(v); return v.back() == 1 ? 0 : 1; }},
    // Positioner i fel behållare (_GLIBCXX_DEBUG).
    {"insert_other", [] { Vec v{1}, w{2}; insert_other(v, w); return 0; }},
    {"erase_other", [] { Vec v{1}, w{2}; erase_other(v, w); return 0; }},
    {"erase_mixed_range", [] { Vec v{1, 2}, w{3, 4}; erase_mixed_range(v, w); return 0; }},
    {"insert_other_direct", [] { Vec v{1}, w{2}; insert_other_direct(v, w); return 0; }},
    {"other_field", [] { Two t{{1}, {2}}; other_field(t); return 0; }},
    {"stale", [] { Vec v{1}; return stale(v) == 1 ? 0 : 1; }},
    {"compare_other", [] { Vec v{1}, w{2}; return compare_other(v, w) ? 0 : 1; }},
    {"append_self", [] { Vec v{1, 2}; append_self(v); return v.size() == 4 ? 0 : 1; }},
    // Stone.
    {"append", [] { Vec v{1}; append(v, 2); return v.back() == 2 ? 0 : 1; }},
    {"prepend", [] { Vec v{1}; prepend(v, 2); return v.front() == 2 ? 0 : 1; }},
    {"drop_first", [] { Vec v{1, 2}; drop_first(v); return v.front() == 2 ? 0 : 1; }},
    {"drop_at", [] { Vec v{1, 2, 3}; drop_at(v, 1); return v[1] == 3 ? 0 : 1; }},
    {"drop_all", [] { Vec v{1, 2}; drop_all(v); return v.empty() ? 0 : 1; }},
    {"insert_local", [] { Vec v{1}; insert_local(v); return v.size() == 2 ? 0 : 1; }},
    {"append_other", [] { Vec v{1}, w{2, 3}; append_other(v, w); return v.size() == 3 ? 0 : 1; }},
    {"erase_loop", [] { Vec v{-1, 2, -3}; erase_loop(v); return v.size() == 1 ? 0 : 1; }},
    {"hint", [] { std::map<int, int> m; hint(m); return m.at(1) == 2 ? 0 : 1; }},
    {"same_field", [] { Two t{{1}, {2}}; same_field(t); return t.a.size() == 2 ? 0 : 1; }},
    {"sort_all", [] { Vec v{3, 1}; sort_all(v); return v[0] == 1 ? 0 : 1; }})
#endif
