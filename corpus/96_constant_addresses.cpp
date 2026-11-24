// Konstanter med statisk lagring som håller en adress (STN-22). Själva
// variabeln ändras aldrig, men det den pekar på kan vara en föränderlig
// global: dolt tillstånd och alias som anroparen inte ser.

int counter = 0;
constexpr int& alias = counter;

struct Box {
  int v;
};
Box box{0};
constexpr Box* box_ptr = &box;

int table[3] = {1, 2, 3};
constexpr int (&table_ref)[3] = table;

struct Holder {
  static constexpr int& ref = counter;
};

struct WithPointer {
  int* p;
};
constexpr WithPointer with_pointer{&counter};

int* const pointers[2] = {&counter, &counter};

constexpr int limit = 10;
constexpr Box constant_box{3};
constexpr int constant_table[3] = {4, 5, 6};

// rules: STN-22
// expect: not-stone: uses variable 'alias' with static storage
int bad_next() {
  alias = alias + 1;
  return alias;
}

// rules: STN-22
// expect: not-stone: uses variable 'box_ptr' with static storage
int bad_peek() { return box_ptr->v; }

// rules: STN-22
// expect: not-stone: uses variable 'table_ref' with static storage
void bad_table_write(int x) { table_ref[0] = x; }

// rules: STN-22
// expect: not-stone: uses variable 'Holder::ref' with static storage
int bad_member_ref() { return Holder::ref; }

// rules: STN-22
// expect: not-stone: expression of type const WithPointer (DeclRefExpr)
void bad_field_pointer(int x) { *with_pointer.p = x; }

// rules: STN-22
// expect: not-stone: unsupported expression: ArraySubscriptExpr
void bad_pointer_table(int x) { *pointers[1] = x; }

// rules: STN-22
// expect: not-stone: uses variable 'alias' with static storage
int bad_alias_param(int& x) {
  x = 1;
  return alias + x;
}

// rules: STN-22
// expect: stone
int ok_limit(int a) { return a < limit ? a : limit; }

// rules: STN-22
// expect: stone
int ok_constant_box() { return constant_box.v; }

// rules: STN-22, STN-75
// expect: stone
int ok_constant_table(int i) { return constant_table[i]; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_next", [] { int a = bad_next(); int b = bad_next(); return a == b ? 0 : 1; }},
    {"bad_peek", [] { int a = bad_peek(); box.v = 42; return bad_peek() == a ? 0 : 1; }},
    {"bad_table_write", [] { bad_table_write(9); return table[0] == 1 ? 0 : 1; }},
    {"bad_member_ref", [] { int a = bad_member_ref(); counter += 5; return bad_member_ref() == a ? 0 : 1; }},
    {"bad_field_pointer", [] { int before = counter; bad_field_pointer(before + 1); return counter == before ? 0 : 1; }},
    {"bad_pointer_table", [] { int before = counter; bad_pointer_table(before + 1); return counter == before ? 0 : 1; }},
    {"bad_alias_param", [] { counter = 0; return bad_alias_param(counter) == 1 ? 0 : 1; }},
    {"ok_limit", [] { return ok_limit(12) == 10 ? 0 : 1; }},
    {"ok_constant_box", [] { return ok_constant_box() == 3 ? 0 : 1; }},
    {"ok_constant_table", [] { return ok_constant_table(1) == 5 ? 0 : 1; }})
#endif
