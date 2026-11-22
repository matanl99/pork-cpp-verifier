// Motståndargenomgång: fall som försöker ta sig förbi Stone.
// bad_* ska vara not-stone, ok_* stone (övriga är hjälpfunktioner med
// dagens dom). Förväntningarna genererades från porks domar efter att
// varje fall granskats; två hål hittades och tätades (implicit
// standardkonstruktor, cleanup-attribut, se 55_constructors och
// 58_destructors). Lägg till nya försök här när en regel ändras.
// G2 dead loans, G3 hidden alias, G4 visible effects, plus today's rules.
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

int global = 0;

// ---- G2 ----
// rules: STN-62
// expect: not-stone
int bad_max_temp(int a) { const int& r = std::max(a, a + 1); return r; }
// rules: STN-62
// expect: not-stone
std::string_view bad_view_temp() { return std::string("abc"); }
// rules: STN-62
// expect: not-stone
int bad_view_local(int a) { std::string_view sv; { std::string s(3, 'x'); sv = s; } return a + static_cast<int>(sv.size()); }
// rules: STN-62
// expect: not-stone
int bad_ref_after_clear(std::vector<int>& v) { if (v.empty()) return 0; int& e = v.front(); v.clear(); return e; }
// rules: STN-62
// expect: not-stone
int bad_ptr_after_push(std::vector<int>& v) { if (v.empty()) return 0; int* p = &v[0]; v.push_back(1); return *p; }
// rules: STN-62
// expect: not-stone
int bad_opt_ref(std::optional<int>& o) { if (!o) return 0; const int& r = *o; o.reset(); return r; }

struct Holder {
  int v = 0;
  // rules: STN-23
  // expect: stone
  const int& get() const { return v; }
};
// rules: STN-23
// expect: stone
Holder make_holder() { return Holder{}; }
// rules: STN-32
// expect: not-stone
int bad_getter_on_temp() { const int& r = make_holder().get(); return r; }

struct Bag {
  std::vector<int> items;
  // rules: STN-23
  // expect: stone: assuming
  void add() { items.push_back(1); }
  // rules: STN-35
  // expect: not-stone
  int bad_loop_calls_add() {
    int s = 0;
    for (int& x : items) { add(); s = s + (x > 0 ? 1 : 0); }
    return s;
  }
};

// rules: STN-62, STN-69
// expect: not-stone
int bad_uptr_get_reset(std::unique_ptr<int>& u) { int* p = u.get(); u.reset(); return p != nullptr ? *p : 0; }
// rules: STN-31
// expect: stone
int bad_ternary_local(const int& a, bool c) { int local = 1; const int& r = c ? a : local; return r; }

// ---- G3 ----
// rules: STN-23
// expect: stone
void two(int& a, int& b) { a = b + 1; }
struct P { int x = 0; int y = 0; };
// rules: STN-23
// expect: stone
void whole_part(P& p, int& x) { p.y = 1; x = 2; }
// rules: STN-62
// expect: not-stone
void bad_same_twice(P& p) { two(p.x, p.x); }
// rules: STN-62
// expect: not-stone
void bad_whole_and_part(P& p) { whole_part(p, p.x); }
// rules: STN-62
// expect: not-stone
void bad_elements(std::vector<int>& v, int i, int j) { two(v[i], v[j]); }

struct S {
  int v = 0;
  // rules: STN-23
  // expect: stone
  void merge(S& o) { v = v + o.v; o.v = 0; }
};
// rules: STN-62
// expect: not-stone
void bad_self_merge(S& s) { s.merge(s); }

// rules: STN-23
// expect: stone
struct G { int v = 0; const int& get() const { return v; } };
// rules: STN-23
// expect: stone
void mod(G& g, const int& x) { g.v = x + 1; }
// rules: STN-62
// expect: not-stone
void bad_getter_alias(G& g) { mod(g, g.get()); }

// rules: STN-23
// expect: stone: assuming
void apply(std::vector<int>& v, int k) { v.push_back(k); }
// rules: STN-40
// expect: stone: assuming
void ok_lambda_result_first(std::vector<int>& v) {
  auto f = [&v]() { return static_cast<int>(v.size()); };
  apply(v, f());
}

// rules: STN-62
// expect: stone
void ok_distinct(P& a, P& b) { two(a.x, b.x); }
// rules: STN-62
// expect: stone
void ok_fields(P& p) { two(p.x, p.y); }

// ---- G4 ----
// rules: STN-23
// expect: not-stone
template <int& R> void set_ref() { R = 1; }
// rules: STN-22, STN-65
// expect: not-stone
void bad_nttp_ref() { set_ref<global>(); }
// rules: STN-23
// expect: not-stone
template <int* Ptr> int read_ptr() { return *Ptr; }
// rules: STN-22, STN-65
// expect: not-stone
int bad_nttp_ptr() { return read_ptr<&global>(); }
// rules: STN-23
// expect: stone
void with_def_ref(int& x = global) { x = 3; }
// rules: STN-22, STN-30
// expect: not-stone
void bad_default_ref_global() { with_def_ref(); }
// rules: STN-19
// expect: not-stone
int bad_const_cast(const int& x) { const_cast<int&>(x) = 1; return x; }
extern int ext;
// rules: STN-22
// expect: not-stone
int bad_extern() { return ext; }
constexpr int kLimit = 10;
// rules: STN-22
// expect: stone
int ok_constexpr_global(int a) { return a < kLimit ? a : kLimit; }

// ---- shallow const / ownership ----
struct V { int x = 0; };
// rules: STN-70
// expect: not-stone
void bad_range_const_uptr(const std::vector<std::unique_ptr<V>>& v) { for (const auto& p : v) if (p) p->x = 1; }
// rules: STN-70
// expect: not-stone
void bad_index_const_uptr(const std::vector<std::unique_ptr<V>>& v) { if (!v.empty() && v[0]) v[0]->x = 1; }
// rules: STN-70
// expect: not-stone
void bad_opt_uptr(const std::optional<std::unique_ptr<V>>& o) { if (o && *o) (*o)->x = 1; }
// rules: STN-69
// expect: not-stone
void bad_uptr_array(const std::unique_ptr<int[]>& a) { if (a) a[0] = 1; }
// rules: STN-69
// expect: not-stone
void bad_adopt_from_other(std::unique_ptr<V>& a, const std::unique_ptr<V>& b) { a.reset(b.get()); }
// rules: STN-69
// expect: not-stone
void bad_release_moved(std::unique_ptr<V>& u) { V* p = std::move(u).release(); (void)p; }
// rules: STN-11
// expect: not-stone
V* bad_capture_ptr(const std::unique_ptr<V>& u) { return u.get(); }
// rules: STN-70
// expect: stone: assuming
int ok_read_const_uptr(const std::vector<std::unique_ptr<V>>& v) { return (!v.empty() && v[0]) ? v[0]->x : 0; }
// rules: STN-70
// expect: stone: assuming
void ok_write_uptr(std::vector<std::unique_ptr<V>>& v) { if (!v.empty() && v[0]) v[0]->x = 1; }

// ---- constructors ----
// rules: STN-23
// expect: not-stone
struct Order { int a; int b; Order(int x) : b(x), a(b) {} };  // reads b before init (declaration order!)
// rules: STN-66
// expect: not-stone
int bad_ctor_order() { Order o(1); return o.a; }
// rules: STN-23
// expect: SelfPass::SelfPass: not-stone
// expect: SelfPass::twice_of: stone
struct SelfPass { int a; int b; SelfPass(int x) : a(x), b(twice_of(a)) {} static int twice_of(const int& v) { return v * 2; } };
// rules: STN-66
// expect: not-stone
int bad_ctor_pass_field() { SelfPass s(1); return s.b; }
// rules: STN-23
// expect: CtorThisCall::CtorThisCall: not-stone
// expect: CtorThisCall::get_a: stone
struct CtorThisCall { int a = 0; int b; CtorThisCall() : b(get_a()) {} int get_a() const { return a; } };
// rules: STN-66
// expect: not-stone
int bad_ctor_method() { CtorThisCall c; return c.b; }

#ifdef PORK_RUN
#include <pork/run.hpp>
int ext = 0;
PORK_RUN_CASES(
    // G2: döda lån som syns vid körning (ASan, _GLIBCXX_DEBUG).
    {"bad_max_temp", [] { return bad_max_temp(1) == 2 ? 0 : 1; }},
    {"bad_ternary_local", [] { int a = 3; return bad_ternary_local(a, true) == 3 && bad_ternary_local(a, false) == 1 ? 0 : 1; }},
    {"bad_view_temp", [] { return bad_view_temp()[0] == 'a' ? 0 : 1; }},
    {"bad_view_local", [] { return bad_view_local(1) == 4 ? 0 : 1; }},
    {"bad_ptr_after_push", [] { std::vector<int> v{7}; return bad_ptr_after_push(v) == 7 ? 0 : 1; }},
    {"bad_getter_on_temp", [] { return bad_getter_on_temp(); }},
    {"bad_uptr_get_reset", [] { auto u = std::make_unique<int>(5); return bad_uptr_get_reset(u) == 5 ? 0 : 1; }},
    {"bad_adopt_from_other", [] {
       auto a = std::make_unique<V>(); auto b = std::make_unique<V>(); bad_adopt_from_other(a, b); return 0; }},
    // Stone: körningen ska vara tyst och ge rätt värde.
    {"Holder::get", [] { Holder h; h.v = 3; return h.get() == 3 ? 0 : 1; }},
    {"make_holder", [] { return make_holder().v; }},
    {"Bag::add", [] { Bag b; b.add(); b.add(); return b.items.size() == 2 ? 0 : 1; }},
    {"two", [] { int a = 0, b = 4; two(a, b); return a == 5 ? 0 : 1; }},
    {"whole_part", [] { P p; int x = 0; whole_part(p, x); return p.y == 1 && x == 2 ? 0 : 1; }},
    {"S::merge", [] { S a{2}, b{3}; a.merge(b); return a.v == 5 && b.v == 0 ? 0 : 1; }},
    {"G::get", [] { G g{4}; return g.get() == 4 ? 0 : 1; }},
    {"mod", [] { G g; int x = 1; mod(g, x); return g.v == 2 ? 0 : 1; }},
    {"apply", [] { std::vector<int> v; apply(v, 3); return v.size() == 1 ? 0 : 1; }},
    {"ok_lambda_result_first", [] {
       std::vector<int> v{1, 2}; ok_lambda_result_first(v); return v.back() == 2 && v.size() == 3 ? 0 : 1; }},
    {"ok_distinct", [] { P a, b; b.x = 1; ok_distinct(a, b); return a.x == 2 ? 0 : 1; }},
    {"ok_fields", [] { P p; p.y = 1; ok_fields(p); return p.x == 2 ? 0 : 1; }},
    {"with_def_ref", [] { int x = 0; global = 0; with_def_ref(x); return x == 3 && global == 0 ? 0 : 1; }},
    {"ok_constexpr_global", [] { return ok_constexpr_global(20) == 10 ? 0 : 1; }},
    {"ok_read_const_uptr", [] {
       std::vector<std::unique_ptr<V>> v; v.push_back(std::make_unique<V>()); v[0]->x = 4;
       return ok_read_const_uptr(v) == 4 ? 0 : 1; }},
    {"ok_write_uptr", [] {
       std::vector<std::unique_ptr<V>> v; v.push_back(std::make_unique<V>()); ok_write_uptr(v);
       return v[0]->x == 1 ? 0 : 1; }},
    {"SelfPass::twice_of", [] { return SelfPass::twice_of(2) == 4 ? 0 : 1; }},
    {"CtorThisCall::get_a", [] { CtorThisCall c; return c.get_a(); }})
#endif
