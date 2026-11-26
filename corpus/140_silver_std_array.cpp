// pork-args: --silver
//
// std::array<T, N> i Silver (SLV-44): samma C-struct som T[N]
// ({ T v[N]; }), så den är ett värde, fält och indata som en ägd array.
// operator[] och at kontrolleras (index < size()), front, back, size, empty
// och fill är modeller; range-for går N varv.

#include <pork/annotations.hpp>

#include <array>
#include <cassert>

using A4 = std::array<int, 4>;

// --- åtkomst ------------------------------------------------------------------

// rules: SLV-44
// expect: stone: not silver: precondition of 'std::array::operator[]': index < size()
int any_index(const A4& a, unsigned long i) { return a[i]; }

// rules: SLV-44, SLV-17
// expect: silver: assuming
int checked_index(const A4& a, unsigned long i) {
  PORK_PRE(i < a.size());
  return a[i];
}

// rules: SLV-44, SLV-43
// expect: stone: not silver: precondition of 'std::array::at': index < size()
int any_at(const A4& a, unsigned long i) { return a.at(i); }

// rules: SLV-44
// expect: silver: assuming
int ends(const A4& a) {
  int f = a.front();
  int b = a.back();
  assert(f == a[0] && b == a[3]);
  return a.empty() ? 0 : static_cast<int>(a.size());
}

// Skrivning genom operator[] och fill.
// rules: SLV-44
// expect: silver: assuming
void write(A4& a) {
  a.fill(2);
  a[1] = 5;
  assert(a[0] == 2 && a[1] == 5 && a[3] == 2);
}

// --- initierare, kopior och fält ---------------------------------------------

// rules: SLV-44
// expect: silver: assuming
int initializers() {
  std::array<int, 3> a{1, 2};
  std::array<int, 2> z{};
  std::array<int, 2> e = {{4, 5}};
  assert(a[1] == 2 && a[2] == 0 && z[1] == 0 && e[1] == 5);
  return a[0] + e[0];
}

// rules: SLV-44
// expect: silver: assuming
int copies(A4& w) {
  A4 a{1, 2, 3, 4};
  A4 b = a;
  b[0] = 9;
  w = a;
  assert(a[0] == 1 && b[0] == 9 && w[3] == 4);
  return b[0];
}

struct Point {
  int x;
  int y;
};

struct Polygon {
  std::array<Point, 3> corners;
  int sides = 3;
};

// rules: SLV-44
// expect: silver: assuming
int field(const Polygon& p) { return p.corners[2].x == 5 ? p.sides : 0; }

// rules: SLV-44
// expect: silver: assuming
int nested() {
  std::array<std::array<int, 3>, 2> m{{{1, 2, 3}, {4, 5, 6}}};
  int s = 0;
  for (const auto& row : m)
    for (int x : row) s += x;
  assert(s == 21 && m[1][2] == 6);
  return s;
}

// Lokala referenser till element blir pekare (Stones lån håller arrayen
// oförändrad medan referensen lever).
// rules: SLV-44
// expect: silver: assuming
int element_refs(A4& a) {
  int& r = a[1];
  r = 4;
  const int& last = a.back();
  assert(a[1] == 4 && last == a[3]);
  return r;
}

// rules: SLV-44, SLV-17
// expect: stone: not silver: precondition of 'std::array::operator[]': index < size()
int element_ref_unchecked(A4& a, unsigned long i) {
  int& r = a[i];
  return r;
}

// --- range-for och tabeller ---------------------------------------------------

// rules: SLV-44
// expect: silver: assuming
void clear(A4& a) {
  for (int& x : a) x = 0;
  assert(a[0] == 0 && a[3] == 0);
}

static constexpr std::array<int, 3> kTable{10, 20, 30};

// rules: SLV-44, SLV-17
// expect: silver: assuming
int table(unsigned long i) {
  PORK_PRE(i < kTable.size());
  int v = kTable[i];
  assert(i != 1 || v == 20);
  return v;
}

// --- inte än ------------------------------------------------------------------

// rules: SLV-44
// expect: stone: not silver: cannot translate to C: std::array::begin (not translated yet)
int iterator(const A4& a) { return *a.begin(); }

// rules: SLV-44
// expect: stone: not silver: cannot translate to C: type struct std::array<int, 0> is not supported
int empty_array(const std::array<int, 0>& a) { return a.empty() ? 1 : 0; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"checked_index", [] { A4 a{1, 2, 3, 4}; return checked_index(a, 3) == 4 ? 0 : 1; }},
    {"ends", [] { A4 a{1, 2, 3, 4}; return ends(a) == 4 ? 0 : 1; }},
    {"write", [] { A4 a{}; write(a); return a[1] == 5 ? 0 : 1; }},
    {"initializers", [] { return initializers() == 5 ? 0 : 1; }},
    {"copies", [] { A4 w{}; return copies(w) == 9 ? 0 : 1; }},
    {"field", [] { Polygon p{{{{1, 2}, {3, 4}, {5, 6}}}}; return field(p) == 3 ? 0 : 1; }},
    {"nested", [] { return nested() == 21 ? 0 : 1; }},
    {"element_refs", [] { A4 a{1, 2, 3, 4}; return element_refs(a) == 4 ? 0 : 1; }},
    {"clear", [] { A4 a{1, 2, 3, 4}; clear(a); return a[2] == 0 ? 0 : 1; }},
    {"table", [] { return table(2) == 30 ? 0 : 1; }},
    {"iterator", [] { A4 a{7, 0, 0, 0}; return iterator(a) == 7 ? 0 : 1; }},
    {"empty_array", [] { std::array<int, 0> a{}; return empty_array(a) == 1 ? 0 : 1; }})
#endif
