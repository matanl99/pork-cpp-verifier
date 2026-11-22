// Defaultargument och defaultinitierare i aggregat (STN-82, granskning 2):
// clangs CFG har dem som löv, korskontrollen följer deras uttryck. Rena
// uttryck är Stone; ett med sidoeffekt avvisas, och kod i namespace pork
// följs som all annan kod (bara kontraktsfunktionerna, på annoteringen,
// undantas).
struct S {
  int v;
  // rules: STN-82
  // expect: stone
  ~S() {}
};

// rules: STN-82
// expect: stone
S make() { return S{1}; }

// rules: STN-82
// expect: stone
int use(int a, int b = make().v) { return a + b; }

struct A { int x = make().v; int y; };

// rules: STN-82
// expect: stone
int default_arg() { return use(1); }

// rules: STN-82
// expect: stone
int aggregate() { A a{.y = 2}; return a.x + a.y; }

int g = 0;

// rules: STN-82
// expect: not-stone
int side() { return ++g; }

// Defaultargumentet utvärderas hos anroparen, inte här.
// rules: STN-82
// expect: stone
int use_side(int a, int b = side()) { return a + b; }

struct B { int x = side(); int y; };

// rules: STN-82
// expect: not-stone: side
int bad_default_arg() { return use_side(1); }

// rules: STN-82
// expect: not-stone: side
int bad_aggregate() { B b{.y = 2}; return b.x + b.y; }

namespace pork {
// rules: STN-82
// expect: not-stone
inline int evil() { return ++g; }
}  // namespace pork

// rules: STN-82
// expect: not-stone: pork::evil
int bad_pork_namespace() { return pork::evil(); }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"default_arg", [] { return default_arg() == 2 ? 0 : 1; }},
               {"aggregate", [] { return aggregate() == 3 ? 0 : 1; }})
#endif
