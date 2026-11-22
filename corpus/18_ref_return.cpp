// pork-args: --silver
//
// Returtyp `const T&` (T värdetyp): resultatet pekar in i `*this` (const
// metod) eller i en `const&`-parameter, aldrig i något som dör vid return.
// Varje return måste vara en väg (variabel/fält) rotad där. Hos anroparen är
// resultatet ett lån av objektet/argumentet, som `v[i]`: det läses, eller
// binds till en lokal `const T&` som lånar objektet (se 13_local_refs.cpp).
// Silver: C-funktionen returnerar värdet (inget kan ändra objektet medan
// resultatet används). Icke-const `T&` som returtyp är fortfarande inte Stone.

#include <vector>

struct Vec2 { int x; int y; };

struct Turn {
  Vec2 target;
  std::vector<int> ids;
  // rules: STN-10, SLV-27
  // expect: silver: assuming
  const Vec2& planned() const { return target; }
  // rules: STN-10, SLV-27
  // expect: silver: assuming
  const std::vector<int>& all() const { return ids; }
  // rules: STN-10, SLV-27
  // expect: silver: assuming
  const int& x() const { return target.x; }
  // rules: STN-7
  // expect: silver: assuming
  void retarget(Vec2 t) { target = t; }
};

// rules: STN-10, SLV-27
// expect: silver
const Vec2& pick(const Vec2& a) { return a; }

// rules: STN-10, SLV-27
// expect: silver
const Vec2& larger(const Vec2& a, const Vec2& b) { return a.x > b.x ? a : b; }

// --- Anroparen ---

// Getterns värde är fältets: x() och planned().x är samma.
// rules: STN-10, SLV-27
// expect: silver: assuming
bool read_getter(const Turn& t) { return t.planned().x == t.x(); }

// rules: STN-10, SLV-27
// expect: silver: assuming
unsigned long count(const Turn& t) { return t.all().size(); }

// Ändringen i loopen kommer efter sista användningen i varvet, men nästa
// varv använder p igen (bindningen är utanför loopen).
// rules: STN-10, STN-62, SLV-42
// expect: stone: not silver: cannot translate to C: const reference 'p' to the result of 'planned' would be a copy in C
int modify_in_loop(Turn& t, int n) {
  const Vec2& p = t.planned();
  int sum = 0;
  for (int i = 0; i < n && i < 3; ++i) {
    sum += p.x;
    t.retarget(Vec2{i, i});
  }
  return sum;
}

// rules: STN-10, STN-62, SLV-27
// expect: silver: assuming
int bound_getter(const Turn& t) { const Vec2& p = t.planned(); return p.x; }

// p lånar fältet t.target; retarget skriver fältet, lånet lever (STN-62).
// Stone; i C vore p en kopia som inte ser ändringen (SLV-42).
// rules: STN-10, STN-62, SLV-42
// expect: stone: not silver: cannot translate to C: const reference 'p' to the result of 'planned' would be a copy in C
// loans: ok
int modify_while_bound(Turn& t) { const Vec2& p = t.planned(); t.retarget(Vec2{1, 2}); return p.x; }

// rules: STN-10, STN-62, SLV-27
// expect: silver: assuming
int modify_after_last_use(Turn& t) { const Vec2& p = t.planned(); int x = p.x; t.retarget(Vec2{1, 2}); return x; }

// Temporär som argument: får läsas i uttrycket, inte bindas.
// rules: STN-10, STN-28
// expect: silver
int read_through_temporary() { return pick(Vec2{1, 2}).x; }
// rules: STN-36
// expect: not-stone: borrows a temporary
int bind_through_temporary() { const Vec2& p = pick(Vec2{1, 2}); return p.x; }

// Två möjliga källor: en const-referens är sin egen rot (STN-36); båda lever.
// rules: STN-10
// expect: silver
int read_larger(const Vec2& a, const Vec2& b) { return larger(a, b).y; }
// rules: STN-36
// expect: silver
int bind_larger(const Vec2& a, const Vec2& b) { const Vec2& p = larger(a, b); return p.y; }

// --- Definitioner som inte är Stone ---

// Värdeparameter: dör vid return.
// rules: STN-10
// expect: not-stone: returns a reference to 'v', which does not outlive the call
const Vec2& from_value(Vec2 v) { return v; }

// Lokal variabel.
// rules: STN-10
// expect: not-stone: returns a reference to 'local', which does not outlive the call
const Vec2& from_local() { const Vec2 local{1, 2}; return local; }

// Temporär.
// rules: STN-10
// expect: not-stone: returns a reference to a temporary
const Vec2& from_temporary() { return Vec2{1, 2}; }

// Icke-const metod: resultatet är ändå const, men *this kan ändras av
// anroparen; bara const-metoder.
struct Holder {
  Vec2 v;
  // rules: STN-10
  // expect: not-stone: return type const Vec2 & needs a const method
  const Vec2& get() { return v; }
};

// rules: STN-9
// expect: not-stone: return type Vec2 &
Vec2& mutable_ref(Vec2& v) { return v; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"modify_while_bound", [] { Turn t{Vec2{7, 8}, {}}; return modify_while_bound(t) == 1 ? 0 : 1; }},
               {"bind_larger", [] { return bind_larger(Vec2{1, 2}, Vec2{3, 4}) == 4 ? 0 : 1; }})
#endif
