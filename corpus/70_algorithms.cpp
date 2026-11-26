// Iteratorer till std-algoritmer (STN-77, STN-78), från gruppexemplet 034.
#include <algorithm>
#include <array>
#include <vector>

int seen = 0;

struct Value {
  int n;
  // rules: STN-78
  // expect: stone
  bool operator<(const Value& o) const { return n < o.n; }
};

struct Counted {
  int n;
  // rules: STN-78, STN-67
  // expect: not-stone: static storage
  bool operator<(const Counted& o) const {
    seen = seen + 1;
    return n < o.n;
  }
};

struct Less {
  // rules: STN-78
  // expect: stone
  bool operator()(const Value& a, const Value& b) const { return a.n < b.n; }
};

// --- godtas -----------------------------------------------------------------

// rules: STN-78
// expect: stone: assuming
void sort_all(std::vector<int>& v) { std::sort(v.begin(), v.end()); }

// rules: STN-78, STN-75
// expect: stone: assuming
int sort_array() {
  std::array<Value, 3> a{{{9}, {2}, {4}}};
  std::sort(a.begin(), a.end());
  return a[0].n;
}

// rules: STN-78, STN-79
// expect: stone: assuming
void sort_lambda(std::vector<Value>& v) {
  std::sort(v.begin(), v.end(), [](const Value& a, const Value& b) { return a.n < b.n; });
}

// rules: STN-78
// expect: stone: assuming
void sort_functor(std::vector<Value>& v) { std::sort(v.begin(), v.end(), Less{}); }

// rules: STN-78
// expect: stone: assuming
void fill_all(std::vector<int>& v, int x) { std::fill(v.begin(), v.end(), x); }

// rules: STN-78
// expect: stone: assuming
void reverse_tail(std::vector<int>& v) {
  if (!v.empty()) std::reverse(v.begin() + 1, v.end());
}

// rules: STN-78, STN-77
// expect: stone: assuming
void copy_other(std::vector<int>& v, const std::vector<int>& w) {
  if (v.size() >= w.size()) std::copy(w.begin(), w.end(), v.begin());
}

// rules: STN-77, STN-58
// expect: stone: assuming
void sort_locals(std::vector<int>& v) {
  auto b = v.begin();
  auto e = v.end();
  std::sort(b, e);
}

// --- avvisas ----------------------------------------------------------------------

// Jämförelsen inne i std::sort är egen kod (STN-67).
// rules: STN-78, STN-67
// expect: not-stone: Counted::operator<
void sort_counted(std::vector<Counted>& v) { std::sort(v.begin(), v.end()); }

// [alg.copy]: result får inte ligga i [first, last). Fanns före STN-77.
// rules: STN-77, STN-58
// expect: not-stone: may point into the same object
void copy_overlap(std::vector<int>& v) {
  auto b = v.begin();
  auto e = v.end();
  auto d = v.begin();
  std::copy(b, e, d);
}

// rules: STN-77, STN-78
// expect: not-stone: may point into the same object
void copy_overlap_direct(std::vector<int>& v) {
  if (v.size() > 1) std::copy(v.begin(), v.end() - 1, v.begin() + 1);
}

// copy_n(first, n, result): inget par, pork vet inte om överlapp är tillåtet.
// rules: STN-77, STN-78
// expect: not-stone: may point into the same object
void copy_n_self(std::vector<int>& v) {
  if (v.size() > 2) std::copy_n(v.begin(), 2, v.begin() + 1);
}

// rules: STN-77, STN-78
// expect: not-stone: may point into the same object
void swap_ends(std::vector<int>& v) {
  if (!v.empty()) std::iter_swap(v.begin(), v.end() - 1);
}

// Inte en sats: sidoeffekten sitter inuti ett uttryck (STN-21).
// rules: STN-78, STN-21
// expect: not-stone: side effect inside expression
int max_of(std::vector<int>& v) { return *std::max_element(v.begin(), v.end()); }

// Fanns före STN-77 med lokala iteratorer.
// rules: STN-77, STN-78
// expect: not-stone: which are not known to be the same object
void sort_mixed(std::vector<int>& v, std::vector<int>& w) { std::sort(v.begin(), w.end()); }

// rules: STN-77, STN-58
// expect: not-stone: which are not known to be the same object
void sort_mixed_locals(std::vector<int>& v, std::vector<int>& w) {
  auto b = v.begin();
  auto e = w.end();
  std::sort(b, e);
}

// first kan komma från v eller w: varje möjligt ursprung ska vara samma
// objekt som last (_GLIBCXX_DEBUG: "requires a valid iterator range").
// rules: STN-77, STN-58
// expect: not-stone: which are not known to be the same object
void sort_conditional(std::vector<int>& v, std::vector<int>& w, bool c) {
  auto a = v.begin();
  auto b = w.begin();
  auto first = c ? a : b;
  auto last = v.end();
  std::sort(first, last);
}

// Samma åt andra hållet.
// rules: STN-77, STN-58
// expect: not-stone: which are not known to be the same object
void sort_conditional_last(std::vector<int>& v, std::vector<int>& w, bool c) {
  auto a = v.end();
  auto b = w.end();
  auto last = c ? a : b;
  std::sort(v.begin(), last);
}

// Algoritmen std::move hade std::move(x):s sammanfattning (identity), så
// lånemodellen såg ingenting. Fanns före rollerna.
// rules: STN-77, STN-58
// expect: not-stone: may point into the same object
void move_overlap(std::vector<int>& v) {
  if (v.size() < 2) return;
  auto b = v.begin();
  auto e = v.end();
  --e;
  auto d = v.begin();
  ++d;
  std::move(b, e, d);
}

// rules: STN-77, STN-58
// expect: not-stone: which are not known to be the same object
void move_mixed(std::vector<int>& v, std::vector<int>& w, std::vector<int>& out) {
  auto b = v.begin();
  auto e = w.end();
  auto d = out.begin();
  std::move(b, e, d);
}

// std::array:s iterator är en pekare: överlagringen move(first, last, d)
// valdes inte, och std::move(x):s identity gällde.
// rules: STN-77
// expect: not-stone: may point into the same object
void move_array_overlap(std::array<int, 3>& a) {
  auto b = a.begin();
  auto e = a.end();
  auto d = a.begin();
  std::move(b, e, d);
}

// rules: STN-77
// expect: not-stone: which are not known to be the same object
void move_array_mixed(std::array<int, 3>& a, std::array<int, 3>& c, std::array<int, 3>& out) {
  std::move(a.begin(), c.end(), out.begin());
}

// En kontextreferens kan vara samma objekt som w, men måste inte: ett
// intervall kräver att ändpunkterna måste vara samma objekt.
struct Ctx {
  std::vector<int> items;
};

struct Sorter {
  Ctx& ctx;
  // rules: STN-77, STN-53
  // expect: not-stone: which are not known to be the same object
  void sort_mixed(std::vector<int>& w) { std::sort(ctx.items.begin(), w.end()); }
  // rules: STN-62, STN-53
  // expect: not-stone: which are not known to be the same object
  bool compare_mixed(std::vector<int>& w) { return ctx.items.begin() == w.end(); }
  // rules: STN-77, STN-53
  // expect: stone: assuming
  void sort_own() { std::sort(ctx.items.begin(), ctx.items.end()); }
};

// En algoritm utan rad i std.txt: rollerna är okända.
// rules: STN-77
// expect: not-stone: have no roles in summaries/std.txt
bool unknown_roles(const std::vector<int>& v, const std::vector<int>& w) {
  return std::includes(v.begin(), v.end(), w.begin(), w.end());
}

// rotate(first, middle, last): tre iteratorer i samma intervall.
// rules: STN-77
// expect: stone: assuming
void rotate_left(std::vector<int>& v) {
  if (!v.empty()) std::rotate(v.begin(), v.begin() + 1, v.end());
}

// rules: STN-77, STN-58
// expect: not-stone: which are not known to be the same object
int find_mixed(const std::vector<int>& v, const std::vector<int>& w) {
  auto b = v.begin();
  auto e = w.end();
  auto it = std::find(b, e, 3);
  return it == b ? 1 : 0;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
using Vec = std::vector<int>;
PORK_RUN_CASES(
    // Intervall och överlapp som körningen kan visa (_GLIBCXX_DEBUG, ASan).
    {"sort_counted", [] { seen = 0; std::vector<Counted> v{{2}, {1}}; sort_counted(v); return seen; }},
    {"sort_mixed", [] { Vec v{3, 2}, w{1, 0}; sort_mixed(v, w); return 0; }},
    {"sort_mixed_locals", [] { Vec v{3, 2}, w{1, 0}; sort_mixed_locals(v, w); return 0; }},
    {"Sorter::sort_mixed", [] { Ctx c{{3, 2}}; Sorter s{c}; Vec w{1, 0}; s.sort_mixed(w); return 0; }},
    {"Sorter::compare_mixed", [] { Ctx c{{3}}; Sorter s{c}; Vec w{1}; return s.compare_mixed(w) ? 1 : 0; }},
    {"find_mixed", [] { Vec v{1}, w{2}; return find_mixed(v, w); }},
    // Stone.
    {"Value::operator<", [] { return Value{1} < Value{2} ? 0 : 1; }},
    {"Less::operator()", [] { return Less{}(Value{1}, Value{2}) ? 0 : 1; }},
    {"sort_all", [] { Vec v{3, 1, 2}; sort_all(v); return v[0] == 1 ? 0 : 1; }},
    {"sort_array", [] { return sort_array() == 2 ? 0 : 1; }},
    {"sort_lambda", [] { std::vector<Value> v{{3}, {1}}; sort_lambda(v); return v[0].n == 1 ? 0 : 1; }},
    {"sort_functor", [] { std::vector<Value> v{{3}, {1}}; sort_functor(v); return v[0].n == 1 ? 0 : 1; }},
    {"fill_all", [] { Vec v(3); fill_all(v, 7); return v[2] == 7 ? 0 : 1; }},
    {"reverse_tail", [] { Vec v{1, 2, 3}; reverse_tail(v); return v[1] == 3 ? 0 : 1; }},
    {"copy_other", [] { Vec v(2), w{4, 5}; copy_other(v, w); return v[1] == 5 ? 0 : 1; }},
    {"sort_locals", [] { Vec v{2, 1}; sort_locals(v); return v[0] == 1 ? 0 : 1; }},
    {"Sorter::sort_own", [] { Ctx c{{3, 1}}; Sorter s{c}; s.sort_own(); return c.items[0] == 1 ? 0 : 1; }},
    {"rotate_left", [] { Vec v{1, 2, 3}; rotate_left(v); return v[0] == 2 ? 0 : 1; }})
#endif
