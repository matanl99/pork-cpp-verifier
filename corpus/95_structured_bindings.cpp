// Structured bindings (STN-84): `auto [a, b] = p;` är en dold lokal variabel
// (en kopia av p, eller med `auto&` en referens till p) och namn på delar av
// den. Den dolda variabeln granskas som vilken lokal som helst; varje bindning
// är en väg in i den: ett fält (`dold.x`) eller, för tuple-lika typer, det
// std::get ger (ett okänt element av den dolda variabeln).
#include <tuple>
#include <utility>
#include <vector>

struct P {
  int x;
  int y;
};

struct Arr {
  int v[2];
};

int g = 0;

// --- Struct ----------------------------------------------------------------

// rules: STN-84
// expect: stone
int ok_copy(P p) {
  auto [a, b] = p;
  return a + b;
}

// rules: STN-84
// expect: stone
int ok_copy_write(P p) {
  auto [a, b] = p;
  a = 5;
  return a + b + p.x;
}

// rules: STN-84, STN-31
// expect: stone
void ok_ref_write(P& p) {
  auto& [a, b] = p;
  a = b;
}

// rules: STN-84, STN-31
// expect: stone
int ok_const_ref(const P& p) {
  const auto& [a, b] = p;
  return a * b;
}

// rules: STN-84, STN-12
// expect: stone
void set(int& out, const int& in) { out = in; }

// rules: STN-84, STN-62
// expect: stone
void ok_two_fields(P& p) {
  auto& [a, b] = p;
  set(a, b);
}

// rules: STN-84, STN-62
// expect: not-stone: may be the same object
void bad_same_field(P& p) {
  auto& [a, b] = p;
  set(a, p.x);
}

// rules: STN-84, STN-22
// expect: not-stone: uses variable 'g' with static storage
int bad_effect_in_init(P p) {
  auto [a, b] = P{g, p.y};
  return a + b;
}

// rules: STN-84
// expect: not-stone: is not initialized
int bad_uninit() {
  P p;
  auto [a, b] = p;
  return a;
}

// rules: STN-84, STN-75
// expect: not-stone
int bad_array_copy(Arr s) {
  auto [a, b] = s.v;
  return a + b;
}

// rules: STN-84
// expect: not-stone
int bad_rvalue_ref(P p) {
  auto&& [a, b] = static_cast<P&&>(p);
  return a;
}

// --- std::pair och std::tuple (betrodda, bindningen går via std::get) -------

// rules: STN-84, STN-4
// expect: stone: assuming std::get, std::pair::pair, type std::pair
int ok_pair(std::pair<int, int> q) {
  auto [first, second] = q;
  return first + second;
}

// rules: STN-84, STN-4
// expect: stone: assuming std::get, type std::pair
void ok_pair_ref(std::pair<int, int>& q) {
  auto& [first, second] = q;
  first = second;
}

// rules: STN-84, STN-4
// expect: stone: assuming std::get, std::tuple::tuple, type std::tuple
int ok_tuple(std::tuple<int, double, bool> t) {
  auto [i, d, b] = t;
  return b ? i : static_cast<int>(d);
}

// rules: STN-84, STN-62
// expect: not-stone: may be the same object
void bad_pair_elements(std::pair<int, int>& q) {
  auto& [first, second] = q;
  set(first, second);
}

// --- Livslängd: lånet bärs av den dolda variabeln (och hållaren) ------------
// Clangs LiveVariables räknar användningen av `a` som levande för bindningen,
// inte för den dolda variabeln. Lånemodellen översätter det: en levande
// bindning håller den dolda variabeln levande, så lånet lever lika länge som
// bindningarna används (också 62_adversarial_rules.cpp, bad_binding_dangling).

// rules: STN-84, STN-62
// expect: not-stone: loans
// loans: rejected: changes 'v' while '[a, b]' borrows
int bad_second_dangles(std::vector<P>& v) {
  auto& [a, b] = v[0];
  v.push_back({1, 2});
  return b;
}

// rules: STN-84, STN-62
// expect: not-stone: loans
// loans: rejected: changes 'v' while '[a, b]' borrows
int bad_pair_dangles(std::vector<std::pair<int, int>>& v) {
  auto& [a, b] = v[0];
  v.clear();
  return a;
}

// rules: STN-84, STN-62
// expect: stone: assuming std::vector::clear, std::vector::operator[], type std::allocator, type std::vector
int ok_copy_then_clear(std::vector<P>& v) {
  auto [a, b] = v[0];
  v.clear();
  return a + b;
}

// rules: STN-84, STN-62
// expect: stone: assuming std::vector::clear, std::vector::operator[], type std::allocator, type std::vector
int ok_used_before_clear(std::vector<P>& v) {
  auto& [a, b] = v[0];
  int s = a + b;
  v.clear();
  return s;
}

// get med ett xvalue tar ingenting (forwards i std.txt), men flytten ut ur
// resultatet förbrukar elementet: lånet in i p.first dör där (P-d).
// rules: STN-84, STN-57
// expect: not-stone
// loans: rejected: changes 'p[*]' while 'e' borrows
int bad_move_through_get(std::pair<std::vector<int>, int> p) {
  const int& e = p.first[0];
  {
    std::vector<int> taken = std::get<0>(std::move(p));
  }
  return e;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"ok_copy", [] { return ok_copy(P{1, 2}) == 3 ? 0 : 1; }},
    {"ok_copy_write", [] { return ok_copy_write(P{1, 2}) == 8 ? 0 : 1; }},
    {"ok_ref_write", [] { P p{1, 2}; ok_ref_write(p); return p.x == 2 && p.y == 2 ? 0 : 1; }},
    {"ok_const_ref", [] { return ok_const_ref(P{3, 4}) == 12 ? 0 : 1; }},
    {"ok_two_fields", [] { P p{1, 7}; ok_two_fields(p); return p.x == 7 ? 0 : 1; }},
    {"bad_same_field", [] { P p{1, 7}; bad_same_field(p); return p.x == 1 ? 0 : 1; }},
    {"ok_pair", [] { return ok_pair({2, 3}) == 5 ? 0 : 1; }},
    {"ok_pair_ref", [] { std::pair<int, int> q{1, 9}; ok_pair_ref(q); return q.first == 9 ? 0 : 1; }},
    {"ok_tuple", [] { return ok_tuple({4, 2.5, true}) == 4 ? 0 : 1; }},
    {"bad_pair_elements", [] { std::pair<int, int> q{1, 9}; bad_pair_elements(q); return q.first == 9 ? 0 : 1; }},
    {"bad_second_dangles", [] { std::vector<P> v{{1, 2}}; v.shrink_to_fit(); return bad_second_dangles(v) == 2 ? 0 : 1; }},
    {"bad_pair_dangles", [] { std::vector<std::pair<int, int>> v{{1, 2}}; return bad_pair_dangles(v) == 1 ? 0 : 1; }},
    {"ok_copy_then_clear", [] { std::vector<P> v{{1, 2}}; return ok_copy_then_clear(v) == 3 ? 0 : 1; }},
    {"ok_used_before_clear", [] { std::vector<P> v{{1, 2}}; return ok_used_before_clear(v) == 3 ? 0 : 1; }},
    {"bad_move_through_get", [] { return bad_move_through_get({{4}, 1}) == 4 ? 0 : 1; }})
#endif
