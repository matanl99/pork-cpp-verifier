// Adversariell granskning (F1): en implicit standardkonstruktor
// initierar varje delobjekt, baser som fält (subobjects(), P-c i
// docs/stone_model.md). En bas med fält utan initierare lämnar dem
// oinitierade: MSan larmade på `D d; return d.x;`.

struct B { int x; int y; };
struct D : B { int z = 0; };  // D() är icke-trivial (NSDMI)

// rules: STN-2, STN-66
// expect: not-stone: base B of D is not initialized
int bad_implicit_default() {
  D d;
  return d.x;
}

struct BP { int x; int y = 1; };
struct DP : BP {};

// rules: STN-2, STN-66
// expect: not-stone: field 'x' of BP is not initialized
int bad_partial_base() {
  DP d;
  return d.x;
}

struct Outer {
  D d;
  int w;
  // rules: STN-66
  // expect: not-stone: base B of D is not initialized
  Outer(int a) : w(a) {}
};

// rules: STN-66
// expect: not-stone: calls non-stone
int bad_nested(int a) {
  Outer o(a);
  return o.d.y;
}

// Basen initieras av sina egna fältinitierare.
struct BI { int x = 1; int y = 2; };
struct DI : BI { int z = 0; };

// rules: STN-2, STN-66
// expect: stone
int full_nsdmi() {
  DI d;
  return d.x + d.z;
}

// En tom bas har inget att initiera.
struct Empty {};
struct DE : Empty { int z = 0; };

// rules: STN-2, STN-66
// expect: stone
int empty_base() {
  DE d;
  return d.z;
}

// Värdeinitiering nollställer basen.
// rules: STN-2
// expect: stone
int value_init() {
  D d{};
  return d.x;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_implicit_default", [] { return bad_implicit_default() == 12345 ? 1 : 0; }},
    {"bad_partial_base", [] { return bad_partial_base() == 12345 ? 1 : 0; }},
    {"bad_nested", [] { return bad_nested(1) == 12345 ? 1 : 0; }},
    {"full_nsdmi", [] { return full_nsdmi() == 1 ? 0 : 1; }},
    {"empty_base", [] { return empty_base(); }},
    {"value_init", [] { return value_init(); }})
#endif
