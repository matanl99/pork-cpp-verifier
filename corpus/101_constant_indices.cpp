// Konstanta index i ägda arrayer (STN-86): values[0] och values[1] är skilda
// element vid anrop (G3). Ett variabelt index, samma index, en annan form eller
// std::array är samma plats values[*].
#include <array>

// rules: STN-86
// expect: stone
void write_pair(int& x, int& y) {
  x = 1;
  y = 2;
}

// rules: STN-86
// expect: stone
void copy_into(int& to, const int& from) { to = from; }

// rules: STN-86, STN-75
// expect: stone
void write_row(int (&row)[2], int& x) {
  row[0] = x;
  x = 3;
}

struct Grid {
  int cells[4];
};

struct Cell {
  int x[2];
};

// --- godtas -----------------------------------------------------------------

// rules: STN-86, STN-75
// expect: stone
int distinct() {
  int values[2] = {0, 0};
  write_pair(values[0], values[1]);
  return values[0] + values[1];
}

// rules: STN-86, STN-75
// expect: stone
void reference_parameter(int (&values)[3]) { write_pair(values[0], values[2]); }

// rules: STN-86, STN-75
// expect: stone
void field_array(Grid& g) { write_pair(g.cells[1], g.cells[3]); }

// En läsning och en skrivning är också skilda element.
// rules: STN-86, STN-75
// expect: stone
void read_and_write(Grid& g) { copy_into(g.cells[0], g.cells[1]); }

// rules: STN-86, STN-75
// expect: stone
int nested() {
  int m[2][2] = {{0, 0}, {0, 0}};
  write_pair(m[0][1], m[1][0]);
  return m[0][1];
}

// Samma rad, olika kolumner.
// rules: STN-86, STN-75
// expect: stone
int same_row() {
  int m[2][2] = {{0, 0}, {0, 0}};
  write_pair(m[1][0], m[1][1]);
  return m[1][1];
}

// rules: STN-86, STN-75
// expect: stone
void array_of_structs(Cell (&c)[2]) { write_pair(c[0].x[1], c[1].x[1]); }

// rules: STN-86, STN-75
// expect: stone
int constant_variable() {
  constexpr int last = 1;
  int values[2] = {0, 0};
  write_pair(values[0], values[last]);
  return values[last];
}

class Board {
 public:
  // rules: STN-86, STN-75
  // expect: stone
  void swap_ends() { write_pair(cells_[0], cells_[3]); }

 private:
  int cells_[4] = {0, 0, 0, 0};
};

// --- underkänns -------------------------------------------------------------

// rules: STN-86, STN-62
// expect: not-stone: loans: operands 'values[*]' and 'values[*]'
int same_index() {
  int values[2] = {0, 0};
  write_pair(values[0], values[0]);
  return values[0];
}

// rules: STN-86, STN-62
// expect: not-stone: loans: operands 'm[*][*]' and 'm[*][*]'
int same_nested_index() {
  int m[2][2] = {{0, 0}, {0, 0}};
  write_pair(m[0][1], m[0][1]);
  return m[0][1];
}

// Ett variabelt index kan vara vilket element som helst.
// rules: STN-86, STN-62
// expect: not-stone: loans: operands 'values[*]' and 'values[*]'
int variable_index(int i) {
  int values[2] = {0, 0};
  write_pair(values[0], values[i]);
  return values[0];
}

// Olika form: en hel rad och ett element i den.
// rules: STN-86, STN-62
// expect: not-stone: loans
int row_and_element() {
  int m[2][2] = {{0, 0}, {0, 0}};
  write_row(m[0], m[0][1]);
  return m[0][0];
}

// Genom en referens: avgörs inte på uttrycket (konservativt).
// rules: STN-86, STN-62
// expect: not-stone: loans
int through_reference() {
  int values[2] = {0, 0};
  int& first = values[0];
  write_pair(first, values[1]);
  return values[0];
}

// std::array: operator[] är ett anrop, inte ett arrayelement (inte med i regeln).
// rules: STN-86, STN-62
// expect: not-stone: loans
int std_array() {
  std::array<int, 2> values{0, 0};
  write_pair(values[0], values[1]);
  return values[0];
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"write_pair", [] { int a = 0, b = 0; write_pair(a, b); return a == 1 && b == 2 ? 0 : 1; }},
    {"copy_into", [] { int a = 0, b = 4; copy_into(a, b); return a == 4 ? 0 : 1; }},
    {"distinct", [] { return distinct() == 3 ? 0 : 1; }},
    {"reference_parameter", [] { int v[3] = {}; reference_parameter(v); return v[0] == 1 && v[2] == 2 ? 0 : 1; }},
    {"field_array", [] { Grid g{}; field_array(g); return g.cells[1] == 1 && g.cells[3] == 2 ? 0 : 1; }},
    {"read_and_write", [] { Grid g{{0, 5, 0, 0}}; read_and_write(g); return g.cells[0] == 5 ? 0 : 1; }},
    {"nested", [] { return nested() == 1 ? 0 : 1; }},
    {"same_row", [] { return same_row() == 2 ? 0 : 1; }},
    {"array_of_structs", [] { Cell c[2] = {}; array_of_structs(c); return c[0].x[1] == 1 && c[1].x[1] == 2 ? 0 : 1; }},
    {"constant_variable", [] { return constant_variable() == 2 ? 0 : 1; }},
    {"Board::swap_ends", [] { Board b; b.swap_ends(); return 0; }})
#endif
