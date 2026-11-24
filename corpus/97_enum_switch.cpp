// En switch över en enum med ett fall för varje uppräknat värde och inget
// default tar ingen gren när enumen har ett annat värde: `static_cast<E>(7)`
// för en enum med fast typ, 3 för en enum utan fast typ med värdena 0..2
// (intervallet är 0..3). Den vägen finns för Stone (STN-17) och Bronze (BRZ-2).

#include <pork/annotations.hpp>

enum class Scoped : int { A, B };
enum Plain { Red, Green, Blue };

// rules: STN-13, STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int bad_scoped(Scoped s) {
  int x;
  switch (s) {
    case Scoped::A: x = 1; break;
    case Scoped::B: x = 2; break;
  }
  return x;
}

// rules: STN-13, STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int bad_plain(Plain p) {
  int x;
  switch (p) {
    case Red: x = 1; break;
    case Green: x = 2; break;
    case Blue: x = 3; break;
  }
  return x;
}

// rules: STN-13, STN-17
// expect: stone
int ok_default(Scoped s) {
  int x;
  switch (s) {
    case Scoped::A: x = 1; break;
    default: x = 2; break;
  }
  return x;
}

// rules: STN-13, STN-17
// expect: stone
int ok_after_switch(Scoped s) {
  switch (s) {
    case Scoped::A: return 1;
    case Scoped::B: return 2;
  }
  return 0;
}

// rules: BRZ-2, BRZ-6
// expect: stone: not bronze: out depends on out, which PORK_DEPENDS does not allow
// expect-violation
void bad_keeps_value(Scoped s, int& out) {
  PORK_DEPENDS(out, s);
  switch (s) {
    case Scoped::A: out = 1; break;
    case Scoped::B: out = 2; break;
  }
}

// rules: BRZ-2
// expect: bronze
void ok_keeps_value(Scoped s, int& out) {
  PORK_DEPENDS(out, s, out);
  switch (s) {
    case Scoped::A: out = 1; break;
    case Scoped::B: out = 2; break;
  }
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_scoped", [] { return bad_scoped(static_cast<Scoped>(7)) == 0 ? 0 : 0; }},
    {"bad_plain", [] { return bad_plain(static_cast<Plain>(3)) == 0 ? 0 : 0; }},
    {"ok_default", [] { return ok_default(static_cast<Scoped>(7)) == 2 ? 0 : 1; }},
    {"ok_after_switch", [] { return ok_after_switch(static_cast<Scoped>(7)); }},
    {"bad_keeps_value", [] { int out = 5; bad_keeps_value(static_cast<Scoped>(7), out); return 0; }},
    {"ok_keeps_value", [] { int out = 5; ok_keeps_value(static_cast<Scoped>(7), out); return out == 5 ? 0 : 1; }})
#endif
