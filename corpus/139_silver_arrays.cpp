// pork-args: --silver
//
// Ägda arrayer (STN-75) i Silver (SLV-44): en array T[N] blir en C-struct
// { T v[N]; }, så den är ett värde som i C++ (kopieras med en struct, kan vara
// fält och indata). Varje indexering kontrolleras: 0 <= i < N.

#include <pork/annotations.hpp>

#include <cassert>

// --- parametrar ---------------------------------------------------------------

// rules: SLV-44
// expect: silver
int bounded_scan(const int (&values)[4], int needle) {
  unsigned i = 0;
  while (i < 4 && values[i] != needle) ++i;
  return static_cast<int>(i);
}

// rules: SLV-44
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int off_by_one_scan(const int (&values)[4], int needle) {
  unsigned i = 0;
  while (i <= 4 && values[i] != needle) ++i;
  return static_cast<int>(i);
}

// rules: SLV-44
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int any_index(const int (&a)[4], unsigned i) { return a[i]; }

// rules: SLV-44, SLV-17
// expect: silver
int checked_index(const int (&a)[4], unsigned i) {
  PORK_PRE(i < 4);
  return a[i];
}

// Ett negativt index är också utanför.
// rules: SLV-44, SLV-17
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int signed_index(const int (&a)[4], int i) {
  PORK_PRE(i < 4);
  return a[i];
}

// rules: SLV-44, SLV-17
// expect: silver
int signed_checked(const int (&a)[4], int i) {
  PORK_PRE(i >= 0 && i < 4);
  return a[i];
}

// rules: SLV-44
// expect: silver
long long sum(const int (&a)[4]) {
  long long s = 0;
  for (unsigned i = 0; i < 4; ++i) s += a[i];
  return s;
}

// Skrivning genom en T (&)[N]-parameter.
// rules: SLV-44
// expect: silver
void fill(int (&a)[3]) {
  a[0] = 1;
  a[1] = a[0] + 1;
  a[2] = a[1] + 1;
}

// Indata är ändliga flyttal, också i en array (annars NaN-resultat).
// rules: SLV-44, SLV-10
// expect: silver
float half(const float (&a)[2]) { return a[1] * 0.5f; }

// --- fält ---------------------------------------------------------------------

struct Board {
  int cells[9];

  // rules: SLV-44, SLV-17
  // expect: silver
  int at(unsigned i) const {
    PORK_PRE(i < 9);
    return cells[i];
  }

  // rules: SLV-44
  // expect: stone: not silver: array index out of bounds
  // expect[esbmc]: stone: not silver: array bounds violated
  int unchecked(unsigned i) const { return cells[i]; }

  // rules: SLV-44, SLV-17
  // expect: silver
  void set(unsigned i, int v) {
    PORK_PRE(i < 9);
    cells[i] = v;
  }
};

// rules: SLV-44
// expect: silver
int center(const Board& b) { return b.cells[4]; }

// En struct med ett arrayfält kopieras som värde.
// rules: SLV-44
// expect: silver
int copy_is_value(Board& b) {
  Board c = b;
  b.cells[0] = 7;
  return c.cells[0] == 7 && b.cells[0] == 7 ? 1 : 0;
}

// --- nästlade arrayer ---------------------------------------------------------

// rules: SLV-44, SLV-17
// expect: silver
int grid(const int (&m)[2][3], unsigned r, unsigned c) {
  PORK_PRE(r < 2 && c < 3);
  return m[r][c];
}

// rules: SLV-44, SLV-17
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int grid_column_off(const int (&m)[2][3], unsigned r, unsigned c) {
  PORK_PRE(r < 2 && c <= 3);
  return m[r][c];
}

// --- anrop --------------------------------------------------------------------

// rules: SLV-44
// expect: silver
int first_of(const int (&a)[4]) { return a[0]; }

// rules: SLV-44
// expect: silver
int calls_first(const int (&a)[4]) { return first_of(a) == a[0] ? 1 : 0; }

// rules: SLV-44
// expect: silver
void set_first(int (&a)[3]) { a[0] = 5; }

// rules: SLV-44
// expect: silver
int calls_set(int (&a)[3]) {
  set_first(a);
  assert(a[0] == 5);
  return a[0];
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bounded_scan", [] { int v[4] = {1, 2, 3, 4}; return bounded_scan(v, 9) == 4 ? 0 : 1; }},
    {"checked_index", [] { int v[4] = {1, 2, 3, 4}; return checked_index(v, 3) == 4 ? 0 : 1; }},
    {"sum", [] { int v[4] = {1, 2, 3, 4}; return sum(v) == 10 ? 0 : 1; }},
    {"fill", [] { int v[3] = {}; fill(v); return v[2] == 3 ? 0 : 1; }},
    {"Board::at", [] { Board b{}; b.cells[8] = 2; return b.at(8) == 2 ? 0 : 1; }},
    {"copy_is_value", [] { Board b{}; return copy_is_value(b) == 0 ? 0 : 1; }},
    {"grid", [] { int m[2][3] = {{1, 2, 3}, {4, 5, 6}}; return grid(m, 1, 2) == 6 ? 0 : 1; }},
    {"calls_set", [] { int v[3] = {}; return calls_set(v) == 5 ? 0 : 1; }})
#endif
