// Referenser som överlever insättning, iteratorer som inte gör det (STN-95).
// [deque.modifiers]: insättning i någon av ändarna ogiltigförklarar alla
// iteratorer men inga referenser till element. [unord.req.general]: en
// rehash ogiltigförklarar iteratorer men inte referenser eller pekare till
// element. Ett lån som en referens eller pekare håller till ett element
// (eller till det elementet äger) lever alltså efter anropet; ett lån som
// en iterator, ett objekt eller en lambda håller dör.
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using D = std::deque<int>;
using Rows = std::deque<std::vector<int>>;
using M = std::unordered_map<int, int>;

// --- godtas -------------------------------------------------------------------

// rules: STN-95
// expect: stone: assuming
int ref_across_push_back(D& d) {
  int& r = d.front();
  d.push_back(1);
  return r;
}

// rules: STN-95
// expect: stone: assuming
int ref_across_push_front(D& d) {
  const int& r = d[0];
  d.push_front(2);
  d.emplace_back(3);
  d.emplace_front(4);
  return r;
}

// rules: STN-95
// expect: stone: assuming
int pointer_across_push_back(D& d) {
  int& last = d.back();
  int* p = &last;
  d.push_back(1);
  return *p;
}

// Det elementet äger flyttas inte heller: elementet själv flyttas inte.
// rules: STN-95
// expect: stone: assuming
int ref_into_element_across_push_back(Rows& d) {
  int& x = d[0][0];
  d.push_back({});
  return x;
}

// rules: STN-95
// expect: stone: assuming
int ref_across_unordered_insert(M& m) {
  int& r = m.at(1);
  m.insert({2, 2});
  m.emplace(3, 3);
  m.try_emplace(4, 4);
  m[5];
  m.reserve(100);
  m.rehash(200);
  return r;
}

// rules: STN-95
// expect: stone: assuming
int ref_across_unordered_set_insert(std::unordered_set<int>& s) {
  auto it = s.find(1);
  const int& r = *it;
  s.insert(2);
  return r;
}

// --- avvisas ------------------------------------------------------------------

// rules: STN-95
// expect: not-stone: loans: changes 'd' while 'it' borrows 'd[*]'
int iterator_across_push_back(D& d) {
  auto it = d.begin();
  d.push_back(1);
  return *it;
}

// En referens till en iterator håller iteratorns lån (och avvisas redan
// av reglerna).
// rules: STN-95
// expect: not-stone: local variable 'ri' has type iterator &
int iterator_reference_across_push_back(D& d) {
  auto it = d.begin();
  auto& ri = it;
  d.push_back(1);
  return *ri;
}

// rules: STN-95
// expect: not-stone: loans: changes 'm' while 'it' borrows 'm[*]'
int iterator_across_unordered_insert(M& m) {
  auto it = m.find(1);
  m.insert({2, 2});
  return it->second;
}

// Insättning mitt i en deque ogiltigförklarar också referenser.
// rules: STN-95
// expect: not-stone: loans: changes 'd' while 'r' borrows 'd[*]'
int ref_across_middle_insert(D& d) {
  int& r = d.back();
  d.insert(d.begin() + 1, 7);
  return r;
}

// pop_front och resize kan ta bort just det element som lånas.
// rules: STN-95
// expect: not-stone: loans: changes 'd' while 'r' borrows 'd[*]'
int ref_across_pop_front(D& d) {
  int& r = d.front();
  d.pop_front();
  return r;
}

// rules: STN-95
// expect: not-stone: loans: changes 'd' while 'r' borrows 'd[*]'
int ref_across_resize(D& d) {
  int& r = d.back();
  d.resize(1);
  return r;
}

// insert_or_assign skriver ett befintligt element: det elementet äger kan
// omallokeras.
// rules: STN-95
// expect: not-stone: loans: changes 'm' while 'r' borrows 'm[*][*]'
int ref_across_insert_or_assign(std::unordered_map<int, std::vector<int>>& m) {
  int& r = m.at(1)[0];
  m.insert_or_assign(1, std::vector<int>(100, 9));
  return r;
}

// Bara referenser och pekare: ett objekt som håller en iterator dör.
// rules: STN-95
// expect: not-stone: loans: changes 'm' while 'res' borrows 'm[*]'
int pair_iterator_across_unordered_insert(M& m) {
  auto res = m.insert({2, 2});
  m.insert({3, 3});
  return res.first->second;
}

// En vector flyttar sina element.
// rules: STN-95
// expect: not-stone: loans: changes 'v' while 'r' borrows 'v[*]'
int ref_across_vector_push_back(std::vector<int>& v) {
  int& r = v.front();
  v.push_back(1);
  return r;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
static D many() {
  D d;
  for (int i = 0; i < 600; ++i) d.push_back(i);
  return d;
}
static M map() { return M{{1, 1}}; }
PORK_RUN_CASES(
    {"ref_across_push_back", [] { D d = many(); return ref_across_push_back(d) == 0 ? 0 : 1; }},
    {"ref_across_push_front", [] { D d = many(); return ref_across_push_front(d) == 0 ? 0 : 1; }},
    {"pointer_across_push_back", [] { D d = many(); return pointer_across_push_back(d) == 599 ? 0 : 1; }},
    {"ref_into_element_across_push_back", [] {
       Rows d(600, std::vector<int>{1});
       return ref_into_element_across_push_back(d) == 1 ? 0 : 1;
     }},
    {"ref_across_unordered_insert", [] { M m = map(); return ref_across_unordered_insert(m) == 1 ? 0 : 1; }},
    {"ref_across_unordered_set_insert", [] {
       std::unordered_set<int> s{1};
       return ref_across_unordered_set_insert(s) == 1 ? 0 : 1;
     }},
    {"iterator_across_push_back", [] { D d = many(); return iterator_across_push_back(d) == 0 ? 0 : 1; }},
    {"iterator_reference_across_push_back", [] {
       D d = many();
       return iterator_reference_across_push_back(d) == 0 ? 0 : 1;
     }},
    {"iterator_across_unordered_insert", [] { M m = map(); return iterator_across_unordered_insert(m) == 1 ? 0 : 1; }},
    {"ref_across_middle_insert", [] { D d = many(); return ref_across_middle_insert(d) == 599 ? 0 : 1; }},
    {"ref_across_pop_front", [] { D d = many(); return ref_across_pop_front(d) == 0 ? 0 : 1; }},
    {"ref_across_resize", [] { D d = many(); return ref_across_resize(d) == 599 ? 0 : 1; }},
    {"ref_across_insert_or_assign", [] {
       std::unordered_map<int, std::vector<int>> m{{1, {1}}};
       return ref_across_insert_or_assign(m) == 1 ? 0 : 1;
     }},
    {"pair_iterator_across_unordered_insert", [] {
       M m = map();
       return pair_iterator_across_unordered_insert(m) == 2 ? 0 : 1;
     }},
    {"ref_across_vector_push_back", [] { std::vector<int> v{1}; return ref_across_vector_push_back(v) == 1 ? 0 : 1; }})
#endif
