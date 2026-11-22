// Motståndargenomgång: fall som försöker ta sig förbi Stone.
// bad_* ska vara not-stone, ok_* stone (övriga är hjälpfunktioner med
// dagens dom). Förväntningarna genererades från porks domar efter att
// varje fall granskats; två hål hittades och tätades (implicit
// standardkonstruktor, cleanup-attribut, se 55_constructors och
// 58_destructors). Lägg till nya försök här när en regel ändras.
// G1: all code that can run is analysed (or a visible assumption).
#include <algorithm>
#include <optional>
#include <vector>
#include <memory>

int global = 0;
// rules: STN-23
// expect: not-stone
int bad_fn(int x) { return x + global; }  // not stone itself (named bad_)

// function pointer call
// rules: STN-18
// expect: not-stone
int bad_fnptr(int x) { int (*f)(int) = &bad_fn; return f(x); }

// default argument calling non-stone function, evaluated in caller
// rules: STN-23
// expect: stone
int with_default(int x, int y = bad_fn(1)) { return x + y; }
// rules: STN-30
// expect: not-stone
int bad_default_arg(int x) { return with_default(x); }

// default member initializer calling non-stone function
struct DM { int v = bad_fn(2); };
// rules: STN-15
// expect: not-stone
int bad_default_member() { DM d{}; return d.v; }

// user-defined literal
// rules: STN-23
// expect: not-stone
int operator""_g(unsigned long long v) { return static_cast<int>(v) + global; }
// rules: STN-18
// expect: not-stone
int bad_udl() { return 5_g; }

// rewritten comparison via user operator<=>
struct Cmp {
  int v;
  // rules: STN-23
  // expect: not-stone
  int operator<=>(const Cmp& o) const { return v - o.v + global; }
};
// rules: STN-23
// expect: not-stone
bool bad_spaceship(Cmp a, Cmp b) { return a < b; }

// static data member
struct Stat { static int count; };
int Stat::count = 0;
// rules: STN-22
// expect: not-stone
int bad_static_member() { return Stat::count; }

// builtin without definition
// rules: STN-18
// expect: not-stone
void bad_builtin(int& a, const int& b) { __builtin_memcpy(&a, &b, sizeof a); }

// implicit default ctor of field whose own user default ctor is non-stone,
// with in-class initializers (defaultInit path)
// rules: STN-23
// expect: not-stone
struct Inner { int v = 0; Inner() { v = global; } };
struct Outer { Inner i; int w = 0; };
// rules: STN-66, STN-67
// expect: not-stone
int bad_implicit_inner() { Outer o{}; return o.w; }
// rules: STN-66, STN-67
// expect: not-stone
int bad_implicit_inner2() { Outer o; return o.w; }

// derived with implicit base default ctor that is non-stone
// rules: STN-23
// expect: not-stone
struct BaseNS { int b = 0; BaseNS() { b = global; } };
// rules: STN-23
// expect: not-stone
struct DerivedNS : BaseNS { int d = 0; DerivedNS() : d(1) {} };
// rules: STN-50, STN-66
// expect: not-stone
int bad_base_default() { DerivedNS x; return x.d; }

// cleanup attribute runs a function at scope end
// rules: STN-14
// expect: not-stone
void cleanup_fn(int* p) { global = *p; }
// rules: STN-14
// expect: not-stone
int bad_cleanup() { int x __attribute__((cleanup(cleanup_fn))) = 1; return x; }

// lambda passed to a trusted std algorithm
// rules: STN-40
// expect: not-stone
void bad_for_each(std::vector<int>& v) { std::for_each(v.begin(), v.end(), [](int& x) { x = global; }); }

// sort with user operator< that is non-stone
// rules: STN-23
// expect: not-stone
struct SK { int k; bool operator<(const SK& o) const { return k < o.k + global; } };
// rules: STN-67
// expect: not-stone
void bad_sort(std::vector<SK>& v) { std::sort(v.begin(), v.end()); }

// immediately invoked lambda touching global
// rules: STN-40
// expect: not-stone
int bad_iife() { return [] { return global; }(); }

// converting constructor that is non-stone
// rules: STN-23
// expect: not-stone
struct Conv { int v; Conv(int a) : v(a + global) {} };
// rules: STN-23
// expect: stone
int takes_conv(Conv c) { return c.v; }
// rules: STN-66
// expect: not-stone
int bad_converting(int a) { return takes_conv(a); }

// vector(n) constructs n defaults via std
// rules: STN-23
// expect: not-stone
struct DefNS { int v; DefNS() : v(global) {} };
// rules: STN-67
// expect: not-stone
int bad_vector_n() { std::vector<DefNS> v(3); return static_cast<int>(v.size()); }

// optional<T>::emplace with no args
// rules: STN-67
// expect: not-stone
int bad_optional_emplace(std::optional<DefNS>& o) { o.emplace(); return 0; }

// custom deleter with user code
// rules: STN-23
// expect: not-stone
struct Del { void operator()(int* p) const { global = *p; delete p; } };
// rules: STN-67, STN-69
// expect: not-stone
void bad_deleter(std::unique_ptr<int, Del>& u) { u.reset(); }

// explicit destructor call
// rules: STN-23
// expect: not-stone
struct DN { int v = 0; ~DN() { v = global; } };
// rules: STN-68
// expect: not-stone
void bad_explicit_dtor(DN& d) { d.~DN(); }

// discarded temporary with non-stone destructor
// rules: STN-23
// expect: not-stone
DN make_dn() { return DN{}; }
// rules: STN-68
// expect: not-stone
void bad_discarded_temp() { make_dn(); }

// non-stone destructor reached through optional reset
// rules: STN-67, STN-68
// expect: not-stone
void bad_optional_reset(std::optional<DN>& o) { o.reset(); }

// controls: these should be fine
// rules: STN-18
// expect: stone
int ok_plain(int x) { return x + 1; }
// rules: STN-23
// expect: stone
struct Good { int v = 0; Good() : v(1) {} };
// rules: STN-67
// expect: stone: assuming
int ok_vector_good() { std::vector<Good> v(3); return static_cast<int>(v.size()); }

#ifdef PORK_RUN
#include <pork/run.hpp>
// G1 syns vid körning bara där icke-Stone-kod skriver: returvärdet visar om
// `global` ändrades (G4).
PORK_RUN_CASES(
    {"bad_deleter", [] {
       global = 0; std::unique_ptr<int, Del> u(new int(7)); bad_deleter(u); return global; }},
    {"with_default", [] { return with_default(1, 2) == 3 ? 0 : 1; }},
    {"takes_conv", [] { global = 0; return takes_conv(Conv(3)) == 3 ? 0 : 1; }},
    {"ok_plain", [] { return ok_plain(1) == 2 ? 0 : 1; }},
    {"Good::Good", [] { Good g; return g.v == 1 ? 0 : 1; }},
    {"ok_vector_good", [] { global = 0; return ok_vector_good() == 3 && global == 0 ? 0 : 1; }})
#endif
