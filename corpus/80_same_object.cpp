// Adversariell granskning (F2, F3): "samma objekt" för intervall,
// jämförelser och positioner (STN-77, STN-74) är en måste-fråga. Ett element
// i en behållare (m[0], m[1]) är okänt: m[*]. Lån som tas genom samma
// bindning av en referens (row i `for (auto& row : m)`) gäller samma objekt.
// Två bas-delobjekt av samma typ har samma väg: avvisas (STN-2).
#include <algorithm>
#include <array>
#include <initializer_list>
#include <span>
#include <vector>

using Rows = std::vector<std::vector<int>>;
using Cube = std::vector<Rows>;

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_two_rows(Rows& m) { std::sort(m[0].begin(), m[1].end()); }

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_two_rows_array(std::array<std::array<int, 4>, 2>& m) { std::sort(m[0].begin(), m[1].end()); }

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_data_rows(std::array<std::array<int, 4>, 2>& m) { std::sort(m[0].data(), m[1].data()); }

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_iter_vars(Rows& m) {
  auto f = m[0].begin();
  auto l = m[1].end();
  std::sort(f, l);
}

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_spans(Rows& m) {
  std::span<int> a(m[0]);
  std::span<int> b(m[1]);
  std::sort(a.begin(), b.end());
}

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_loop_rows(Rows& m) {
  for (auto& a : m)
    for (auto& b : m) std::sort(a.begin(), b.end());
}

// rules: STN-77, STN-71
// expect: not-stone: not known to be the same object
int bad_for_each_two_lists(std::initializer_list<std::initializer_list<int>> in) {
  for (const auto& a : in)
    for (const auto& b : in) std::for_each(a.begin(), b.end(), [](int x) { (void)x; });
  return 0;
}

// rules: STN-62
// expect: not-stone: not known to be the same object
bool bad_compare_rows(Rows& m) { return m[0].begin() == m[1].begin(); }

// rules: STN-74
// expect: not-stone: must point into
void bad_insert_other_row(Rows& m) { m[0].insert(m[1].begin(), 5); }

// rules: STN-74
// expect: not-stone: must point into
void bad_erase_other_row(Rows& m) { m[0].erase(m[1].begin()); }

// rules: STN-74
// expect: not-stone: must point into
void bad_erase_range_rows(Rows& m) { m[0].erase(m[0].begin(), m[1].end()); }

// Inne i en rad: row[0] och row[1] är olika.
// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_cube(Cube& c) {
  for (auto& m : c) std::sort(m[0].begin(), m[1].end());
}

// En iterator från förra varvet: row är en ny bindning.
// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_stale(Rows& m) {
  std::vector<int>::iterator first;
  bool have = false;
  for (auto& row : m) {
    if (have) std::sort(first, row.end());
    first = row.begin();
    have = true;
  }
}

// rules: STN-74
// expect: not-stone: must point into
void bad_stale_erase(Rows& m) {
  std::vector<int>::iterator first;
  bool have = false;
  for (auto& row : m) {
    if (have) row.erase(first);
    first = row.begin();
    have = true;
  }
}

// rules: STN-62
// expect: not-stone: not known to be the same object
bool bad_compare_stale(Rows& m) {
  std::vector<int>::iterator first;
  bool have = false, eq = false;
  for (auto& row : m) {
    if (have) eq = eq || first == row.end();
    first = row.begin();
    have = true;
  }
  return eq;
}

// rules: STN-77
// expect: not-stone: not known to be the same object
void bad_sort_two_vectors(std::vector<int>& a, std::vector<int>& b) { std::sort(a.begin(), b.end()); }

// rules: STN-77
// expect: stone: assuming
void sort_rows(Rows& m) {
  for (auto& row : m) std::sort(row.begin(), row.end());
}

// rules: STN-77
// expect: stone: assuming
int sum_rows(const Rows& m) {
  int s = 0;
  for (const auto& row : m)
    for (int x : row) s += x;
  return s;
}

// rules: STN-77
// expect: stone: assuming
void sort_first(Rows& m) {
  auto& r = m[0];
  std::sort(r.begin(), r.end());
}

// rules: STN-77
// expect: stone: assuming
void sort_alias(Rows& m) {
  auto& r = m[0];
  auto& q = r;
  std::sort(r.begin(), q.end());
}

// rules: STN-74
// expect: stone: assuming
void drop_first(Rows& m) {
  for (auto& row : m)
    if (!row.empty()) row.erase(row.begin());
}

// rules: STN-71
// expect: stone: assuming
int sum_lists(std::initializer_list<std::initializer_list<int>> in) {
  int s = 0;
  for (const auto& row : in)
    for (int x : row) s += x;
  return s;
}

// F3: två B-delobjekt som vägarna inte skiljer åt.
struct B { std::vector<int> v; };
struct L : B {};
struct R : B {};
struct D : L, R {};

// rules: STN-2
// expect: not-stone: has base B more than once
void bad_sort_two_subobjects(D& d) { std::sort(d.L::v.begin(), d.R::v.end()); }

// rules: STN-2
// expect: not-stone: has base B more than once
void bad_erase_subobjects(D& d) { d.L::v.erase(d.R::v.begin()); }

// En tom bas två gånger delar inget: tillåten.
struct Tag {};
struct T1 : Tag {};
struct T2 : Tag {};
struct Tagged : T1, T2 { int x; };

// rules: STN-2
// expect: stone
int tagged(const Tagged& t) { return t.x; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_sort_two_rows", [] { Rows m{{3, 2, 1}, {6, 5, 4}}; bad_sort_two_rows(m); return 0; }},
    {"bad_sort_two_rows_array", [] { std::array<std::array<int, 4>, 2> m{}; bad_sort_two_rows_array(m); return 0; }},
    {"bad_sort_iter_vars", [] { Rows m{{3, 2, 1}, {6, 5, 4}}; bad_sort_iter_vars(m); return 0; }},
    {"bad_sort_loop_rows", [] { Rows m{{3, 2, 1}, {6, 5, 4}}; bad_sort_loop_rows(m); return 0; }},
    {"bad_for_each_two_lists", [] { return bad_for_each_two_lists({{1, 2}, {3}}); }},
    {"bad_compare_rows", [] { Rows m{{1}, {2}}; return bad_compare_rows(m) ? 1 : 0; }},
    {"bad_insert_other_row", [] { Rows m{{1}, {2}}; bad_insert_other_row(m); return 0; }},
    {"bad_erase_other_row", [] { Rows m{{1}, {2}}; bad_erase_other_row(m); return 0; }},
    {"bad_erase_range_rows", [] { Rows m{{1, 2}, {3, 4}}; bad_erase_range_rows(m); return 0; }},
    {"bad_stale", [] { Rows m{{3, 2}, {1, 0}}; bad_stale(m); return 0; }},
    {"bad_stale_erase", [] { Rows m{{3, 2}, {1, 0}}; bad_stale_erase(m); return 0; }},
    {"bad_compare_stale", [] { Rows m{{3, 2}, {1, 0}}; return bad_compare_stale(m) ? 1 : 0; }},
    {"bad_sort_two_vectors", [] { std::vector<int> a{3, 2}, b{1, 0}; bad_sort_two_vectors(a, b); return 0; }},
    {"sort_rows", [] { Rows m{{3, 2, 1}, {6, 5, 4}}; sort_rows(m); return m[0][0] == 1 && m[1][0] == 4 ? 0 : 1; }},
    {"sum_rows", [] { Rows m{{1, 2}, {3}}; return sum_rows(m) == 6 ? 0 : 1; }},
    {"sort_first", [] { Rows m{{3, 2, 1}}; sort_first(m); return m[0][0] == 1 ? 0 : 1; }},
    {"sort_alias", [] { Rows m{{3, 2, 1}}; sort_alias(m); return m[0][0] == 1 ? 0 : 1; }},
    {"drop_first", [] { Rows m{{1, 2}, {}}; drop_first(m); return m[0].size() == 1 ? 0 : 1; }},
    {"sum_lists", [] { return sum_lists({{1, 2}, {3}}) == 6 ? 0 : 1; }},
    {"bad_sort_two_subobjects", [] { D d; d.L::v = {3, 2, 1}; d.R::v = {6, 5, 4}; bad_sort_two_subobjects(d); return 0; }},
    {"bad_erase_subobjects", [] { D d; d.L::v = {1}; d.R::v = {2}; bad_erase_subobjects(d); return 0; }})
#endif
