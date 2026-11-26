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

// --- lokala arrayer, initierare och tabeller ------------------------------------

// rules: SLV-44
// expect: silver
int local_array() {
  int a[3] = {1, 2};
  assert(a[1] == 2 && a[2] == 0);
  return a[0] + a[1] + a[2];
}

// rules: SLV-44, SLV-17
// expect: silver
int local_zero(unsigned i) {
  PORK_PRE(i < 5);
  int z[5] = {};
  z[i] = 4;
  return z[i];
}

// En lokal array skrivs och läses, och indexet kontrolleras också där.
// rules: SLV-44
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int local_off(unsigned n) {
  int a[4] = {};
  for (unsigned i = 0; i < n; ++i) a[i] = 1;
  return a[0];
}

// rules: SLV-44
// expect: silver
int nested_init() {
  int m[2][2] = {{1, 2}, {3}};
  assert(m[0][1] == 2 && m[1][0] == 3 && m[1][1] == 0);
  return m[0][1] + m[1][0] + m[1][1];  // 2 + 3 + 0
}

struct Point {
  int x;
  int y;
};

struct Shape {
  int sides[3];
  Point corners[2];
};

// rules: SLV-44
// expect: silver
int aggregate_with_arrays() {
  Shape s{{1, 2, 3}, {{1, 2}, {3, 4}}};
  Shape e{};
  assert(s.sides[2] == 3 && s.corners[1].y == 4 && e.sides[1] == 0 && e.corners[0].x == 0);
  return s.sides[2] + s.corners[1].y + e.sides[1] + e.corners[0].x;  // 3 + 4 + 0 + 0
}

// Fyllnaden är ett element med fältinitierare, inte noll.
struct Cell {
  int value = 7;
};

// rules: SLV-44
// expect: silver
int filler_with_initializer() {
  Cell cs[3] = {Cell{1}};
  assert(cs[0].value == 1 && cs[2].value == 7);
  return cs[0].value + cs[2].value;  // 1 + 7
}

// Ett arrayfält med initierare i klassen och i en konstruktor.
struct Counters {
  int hits[2] = {5, 6};
  int misses[2];
  // rules: SLV-44, SLV-39
  // expect: silver
  Counters() : misses{} {}
};

// rules: SLV-44
// expect: silver
int member_initializers() {
  Counters c;
  assert(c.hits[1] == 6 && c.misses[0] == 0);
  return c.hits[1] + c.misses[0];  // 6 + 0
}

// Element med implicit standardkonstruktor: fältinitierarna i varje element.
// rules: SLV-44
// expect: silver
int default_elements() {
  Cell cs[3];
  cs[1].value = 2;
  assert(cs[0].value == 7 && cs[1].value == 2 && cs[2].value == 7);
  return cs[2].value;
}

// En egen konstruktor per element översätts inte än.
struct Counted {
  int n;
  // rules: SLV-39
  // expect: silver
  Counted() : n(1) {}
};

// rules: SLV-44
// expect: stone: not silver: cannot translate to C: array of Counted built by a constructor (not translated yet)
int constructed_elements() {
  Counted cs[2];
  return cs[1].n;
}

static const int kTable[4] = {10, 20, 30, 40};
static const char kName[] = "abc";

// En tabell ur en strängliteral (med avslutande noll).
// rules: SLV-44
// expect: silver
int name_char(unsigned i) {
  int c = i < 4 ? kName[i] : -1;
  assert(i != 1 || c == 'b');
  assert(i != 3 || c == 0);
  return c;
}

// rules: SLV-44, SLV-17
// expect: silver
int table_lookup(unsigned i) {
  PORK_PRE(i < 4);
  int v = kTable[i];
  assert(i != 2 || v == 30);
  return v;
}

// rules: SLV-44
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int table_any(unsigned i) { return kTable[i]; }

// rules: SLV-44, SLV-17
// expect: silver
int static_local_table(unsigned i) {
  PORK_PRE(i < 3);
  static const Point corners[3] = {{0, 0}, {1, 0}, {0, 1}};
  assert(i != 1 || (corners[i].x == 1 && corners[i].y == 0));
  return corners[i].x + corners[i].y;
}

// En lokal referens till ett element blir en pekare.
// rules: SLV-44
// expect: silver
int element_ref(int (&a)[3]) {
  int& r = a[2];
  r = 8;
  assert(a[2] == 8);
  return r;
}

// rules: SLV-44
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: pointer arithmetic out of bounds
int element_ref_any(const int (&a)[3], unsigned i) {
  const int& r = a[i];
  return r;
}

// --- range-for ----------------------------------------------------------------

// rules: SLV-44
// expect: silver
long long range_sum(const int (&a)[4]) {
  long long s = 0;
  for (int x : a) s += x;
  return s;
}

// rules: SLV-44
// expect: silver
int range_values() {
  int a[3] = {4, 5, 6};
  int last = 0;
  int count = 0;
  for (const int& x : a) {
    last = x;
    ++count;
  }
  assert(last == 6 && count == 3);
  return last;
}

// Skrivning genom loopvariabeln.
// rules: SLV-44
// expect: silver
void range_clear(int (&a)[3]) {
  for (int& x : a) x = 0;
  assert(a[0] == 0 && a[2] == 0);
}

// rules: SLV-44
// expect: silver
int range_rows() {
  int m[2][3] = {{1, 2, 3}, {4, 5, 6}};
  int s = 0;
  for (const auto& row : m)
    for (int x : row) s += x;
  assert(s == 21);
  return s;
}

// rules: SLV-44
// expect: silver
int range_table() {
  int s = 0;
  for (int x : kTable) s += x;
  assert(s == 100);
  return s;
}

// rules: SLV-44
// expect: silver
int range_field(const Board& b) {
  int n = 0;
  for (int c : b.cells) n += c == 0 ? 1 : 0;
  return n;
}

// Ett index ur loopen kontrolleras fortfarande.
// rules: SLV-44
// expect: stone: not silver: array index out of bounds
// expect[esbmc]: stone: not silver: array bounds violated
int range_then_index(const int (&a)[4]) {
  for (int x : a)
    if (x != 0) return a[x];
  return 0;
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
    {"calls_set", [] { int v[3] = {}; return calls_set(v) == 5 ? 0 : 1; }},
    {"local_array", [] { return local_array() == 3 ? 0 : 1; }},
    {"local_zero", [] { return local_zero(4) == 4 ? 0 : 1; }},
    {"nested_init", [] { return nested_init() == 5 ? 0 : 1; }},
    {"aggregate_with_arrays", [] { return aggregate_with_arrays() == 7 ? 0 : 1; }},
    {"filler_with_initializer", [] { return filler_with_initializer() == 8 ? 0 : 1; }},
    {"member_initializers", [] { return member_initializers() == 6 ? 0 : 1; }},
    {"table_lookup", [] { return table_lookup(3) == 40 ? 0 : 1; }},
    {"default_elements", [] { return default_elements() == 7 ? 0 : 1; }},
    {"name_char", [] { return name_char(1) == 'b' ? 0 : 1; }},
    {"static_local_table", [] { return static_local_table(2) == 1 ? 0 : 1; }},
    {"element_ref", [] { int v[3] = {}; return element_ref(v) == 8 ? 0 : 1; }},
    {"range_sum", [] { int v[4] = {1, 2, 3, 4}; return range_sum(v) == 10 ? 0 : 1; }},
    {"range_values", [] { return range_values() == 6 ? 0 : 1; }},
    {"range_clear", [] { int v[3] = {1, 2, 3}; range_clear(v); return v[1] == 0 ? 0 : 1; }},
    {"range_rows", [] { return range_rows() == 21 ? 0 : 1; }},
    {"range_table", [] { return range_table() == 100 ? 0 : 1; }},
    {"range_field", [] { Board b{}; return range_field(b) == 9 ? 0 : 1; }})
#endif
