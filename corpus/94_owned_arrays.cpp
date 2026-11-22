// Ägda arrayer (STN-75): en inbyggd array med storlek känd vid kompileringen
// och element utan egen kod vid kopiering och destruktion är ägd lagring, var
// den än står: fält, lokal variabel, referensparameter, konstant tabell.
// Indexering, range-for och referenser till rader godtas; konverteringen till
// pekare bara som del av indexeringen.
#include <string>

int rt();

struct P {
  int x;
  int y;
};

struct Q {
  int v;
  // rules: STN-66
  // expect: stone
  Q() : v(1) {}
};

struct C {
  int v;
  C() = default;
  // rules: STN-72
  // expect: stone
  C(const C& o) : v(o.v + 1) {}
};

struct D {
  int v = 0;
  // rules: STN-68
  // expect: stone
  ~D() {}
};

// --- Lokala arrayer ---------------------------------------------------------

// rules: STN-75, STN-14
// expect: stone
int ok_local(int i) {
  int a[3] = {1, 2, 3};
  return a[i];
}

// rules: STN-75, STN-14
// expect: stone
int ok_local_fill() {
  int a[3] = {1};
  return a[2];
}

// rules: STN-75, STN-14
// expect: stone
int ok_local_string(int i) {
  char s[] = "abc";
  return s[i];
}

// rules: STN-75, STN-14
// expect: stone
int ok_local_write(int i) {
  int a[3] = {};
  a[i] = 4;
  return a[0] + a[1] + a[2];
}

// rules: STN-75, STN-15
// expect: not-stone: local variable 'a' is not initialized
int bad_uninit(int i) {
  int a[3];
  return a[i];
}

// rules: STN-75, STN-15
// expect: not-stone: the elements of an array of P are not initialized
int bad_uninit_struct() {
  P p[2];
  return p[0].x;
}

// rules: STN-75, STN-66
// expect: stone
int ok_default_ctor() {
  Q q[2];
  return q[1].v;
}

// rules: STN-75
// expect: stone
int ok_structs(int i) {
  P p[2] = {{1, 2}, {3, 4}};
  return p[i].y;
}

// rules: STN-75
// expect: stone
int ok_grid(int i, int j) {
  int g[2][2] = {{1, 2}, {3, 4}};
  return g[i][j];
}

// rules: STN-75, STN-31
// expect: stone
int ok_row_ref() {
  int g[2][2] = {};
  auto& r = g[1];
  r[0] = 3;
  return g[1][0];
}

// --- Element med egen kod ---------------------------------------------------

// rules: STN-75, STN-72
// expect: not-stone: element type C is not trivially copyable
int bad_copy_elem() {
  C c[2] = {};
  return c[0].v;
}

// rules: STN-75, STN-68
// expect: not-stone: element type D is not trivially copyable
int bad_dtor_elem() {
  D d[2] = {};
  return d[0].v;
}

// rules: STN-75
// expect: not-stone: element type std::string is not trivially copyable
int bad_string_elem() {
  std::string s[2] = {"a", "b"};
  return static_cast<int>(s[0].size());
}

// --- Statisk lagring --------------------------------------------------------

constexpr int kTable[4] = {1, 2, 4, 8};
const int kConstTable[2] = {4, 5};
const int kDynamic[2] = {rt(), 1};
int gMutable[3] = {1, 2, 3};
extern const int kExtern[3];

// rules: STN-75, STN-22
// expect: stone
int ok_table(int i) { return kTable[i]; }

// rules: STN-75, STN-22
// expect: stone
int ok_const_table(int i) { return kConstTable[i]; }

// rules: STN-75, STN-14
// expect: stone
int ok_static_local(int i) {
  static constexpr int t[3] = {5, 6, 7};
  return t[i];
}

// rules: STN-75, STN-22
// expect: not-stone: uses variable 'kDynamic' with static storage
int bad_dynamic_table(int i) { return kDynamic[i]; }

// rules: STN-75, STN-22
// expect: not-stone: uses variable 'gMutable' with static storage
int bad_mutable_table(int i) { return gMutable[i]; }

// rules: STN-75, STN-22
// expect: not-stone: uses variable 'kExtern' with static storage
int bad_extern_table(int i) { return kExtern[i]; }

// rules: STN-75, STN-14
// expect: not-stone: has static storage
int bad_static_mutable(int i) {
  static int t[3] = {1, 2, 3};
  return t[i]++;
}

// --- Referensparametrar och alias -------------------------------------------

// rules: STN-75, STN-12
// expect: stone
int ok_param(const int (&a)[3], int i) { return a[i]; }

// rules: STN-75, STN-12
// expect: stone
void set_first(int (&a)[3], const int (&b)[3]) { a[0] = b[1]; }

// rules: STN-75, STN-12
// expect: stone
void set_middle(int (&a)[3], const int& v) { a[1] = v; }

// rules: STN-75, STN-62
// expect: stone
void ok_two_arrays(int (&a)[3], int (&b)[3]) { set_first(a, b); }

// rules: STN-75, STN-62
// expect: not-stone: may be the same object
void bad_same_array(int (&a)[3]) { set_first(a, a); }

// rules: STN-75, STN-62
// expect: not-stone: may be the same object
void bad_array_and_element(int (&a)[3]) { set_middle(a, a[1]); }

template <unsigned long N>
// rules: STN-75, STN-6
// expect: stone
int sum(const int (&a)[N]) {
  int s = 0;
  for (int x : a) s += x;
  return s;
}

// rules: STN-75, STN-23
// expect: stone
int ok_template() {
  int a[4] = {1, 2, 3, 4};
  return sum(a);
}

// --- Range-for --------------------------------------------------------------

// rules: STN-75
// expect: stone
int ok_range_local() {
  int a[3] = {1, 2, 3};
  int s = 0;
  for (const int& x : a) s += x;
  return s;
}

// rules: STN-75
// expect: stone
void ok_range_write(int (&a)[3]) {
  for (int& x : a) x = 0;
}

// rules: STN-75, STN-22
// expect: stone
int ok_range_table() {
  int s = 0;
  for (int x : kTable) s += x;
  return s;
}

// rules: STN-75
// expect: stone
int ok_range_rows() {
  int g[2][2] = {{1, 2}, {3, 4}};
  int s = 0;
  for (const auto& row : g)
    for (int x : row) s += x;
  return s;
}

// --- Det som inte är indexering ---------------------------------------------

// rules: STN-75, STN-19
// expect: not-stone: unsupported cast: ArrayToPointerDecay
int bad_decay() {
  int a[2] = {1, 2};
  int* p = a;
  return *p;
}

// rules: STN-75, STN-18
// expect: not-stone: pointer arithmetic
int bad_pointer_arithmetic(int (&a)[3]) {
  int* p = &a[0];
  int* q = p + 1;
  return *q;
}

// rules: STN-75
// expect: not-stone: lambda captures 'a' of type int[2]
int bad_capture() {
  int a[2] = {1, 2};
  auto f = [a]() { return a[0]; };
  return f();
}

#ifdef PORK_RUN
#include <pork/run.hpp>
const int kExtern[3] = {1, 2, 3};
int rt() { return 7; }
PORK_RUN_CASES(
    {"ok_local", [] { return ok_local(2) == 3 ? 0 : 1; }},
    {"ok_local_fill", [] { return ok_local_fill(); }},
    {"ok_local_string", [] { return ok_local_string(1) == 'b' ? 0 : 1; }},
    {"ok_local_write", [] { return ok_local_write(1) == 4 ? 0 : 1; }},
    {"ok_default_ctor", [] { return ok_default_ctor() == 1 ? 0 : 1; }},
    {"ok_structs", [] { return ok_structs(1) == 4 ? 0 : 1; }},
    {"ok_grid", [] { return ok_grid(1, 0) == 3 ? 0 : 1; }},
    {"ok_row_ref", [] { return ok_row_ref() == 3 ? 0 : 1; }},
    {"ok_table", [] { return ok_table(3) == 8 ? 0 : 1; }},
    {"ok_const_table", [] { return ok_const_table(1) == 5 ? 0 : 1; }},
    {"ok_static_local", [] { return ok_static_local(2) == 7 ? 0 : 1; }},
    {"ok_param", [] { int a[3] = {1, 2, 3}; return ok_param(a, 2) == 3 ? 0 : 1; }},
    {"ok_two_arrays", [] { int a[3] = {}; int b[3] = {0, 9, 0}; ok_two_arrays(a, b); return a[0] == 9 && b[0] == 0 ? 0 : 1; }},
    {"bad_same_array", [] { int a[3] = {1, 2, 3}; bad_same_array(a); return a[0] == 2 ? 0 : 1; }},
    {"bad_array_and_element", [] { int a[3] = {1, 2, 3}; bad_array_and_element(a); return a[1] == 2 ? 0 : 1; }},
    {"ok_template", [] { return ok_template() == 10 ? 0 : 1; }},
    {"ok_range_local", [] { return ok_range_local() == 6 ? 0 : 1; }},
    {"ok_range_write", [] { int a[3] = {1, 2, 3}; ok_range_write(a); return a[0] + a[1] + a[2]; }},
    {"ok_range_table", [] { return ok_range_table() == 15 ? 0 : 1; }},
    {"ok_range_rows", [] { return ok_range_rows() == 10 ? 0 : 1; }})
#endif
