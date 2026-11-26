// Hur en egen funktion ändrar sina parametrar (STN-91, STN-95 genom
// sammanfattningar): sammanfattningen säger för varje skriven plats om alla
// skrivningar dit behåller elementens värden (push_back i en behållare av
// std::unique_ptr, STN-91) eller bara ogiltigförklarar iteratorer (insättning
// i ändarna av en deque, STN-95). Hos anroparen gäller då samma sak som vid
// ett direkt anrop. En annan skrivning till samma plats i funktionen (clear,
// erase, reset, en annan gren) gör det till en vanlig skrivning.
#include <deque>
#include <memory>
#include <utility>
#include <vector>

struct R {
  int value;
};

using Owners = std::vector<std::unique_ptr<R>>;
using D = std::deque<int>;

// rules: STN-91
// expect: stone: assuming
void add(Owners& v) { v.push_back(std::make_unique<R>(R{0})); }

// rules: STN-91
// expect: stone: assuming
void add_twice(Owners& v) {
  add(v);
  v.emplace_back(std::make_unique<R>(R{1}));
}

// rules: STN-95
// expect: stone: assuming
void push(D& d) { d.push_back(1); }

// rules: STN-91
// expect: stone: assuming
void clear_and_add(Owners& v) {
  v.clear();
  v.push_back(std::make_unique<R>(R{0}));
}

// rules: STN-91
// expect: stone: assuming
void add_or_erase(Owners& v, bool more) {
  if (more) v.push_back(std::make_unique<R>(R{0}));
  else v.erase(v.begin());
}

// rules: STN-91
// expect: stone: assuming
void reset_first(Owners& v) { v[0].reset(); }

struct Pool {
  Owners items;
  // rules: STN-91
  // expect: stone: assuming
  void grow() { items.push_back(std::make_unique<R>(R{2})); }
};

// --- godtas -------------------------------------------------------------------

// rules: STN-91
// expect: stone: assuming
int pointee_across_helper(Owners& v) {
  R& b = *v[0];
  add(v);
  return b.value;
}

// Två nivåer: add_twice anropar add.
// rules: STN-91
// expect: stone: assuming
int pointee_across_two_levels(Owners& v) {
  R& b = *v.front();
  add_twice(v);
  return b.value;
}

// rules: STN-91
// expect: stone: assuming
int pointee_across_method(Pool& p) {
  R& b = *p.items[0];
  p.grow();
  return b.value;
}

// rules: STN-95
// expect: stone: assuming
int reference_across_helper(D& d) {
  int& r = d.front();
  push(d);
  return r;
}

// --- avvisas ------------------------------------------------------------------

// Elementet självt flyttas vid omallokering.
// rules: STN-91
// expect: not-stone: loans: changes 'v' while 'p' borrows 'v[*]'
int element_across_helper(Owners& v) {
  auto& p = v[0];
  add(v);
  return p->value;
}

// rules: STN-91
// expect: not-stone: loans: changes 'v' while 'b' borrows 'v[*][*]'
int pointee_across_clear(Owners& v) {
  R& b = *v[0];
  clear_and_add(v);
  return b.value;
}

// rules: STN-91
// expect: not-stone: loans: changes 'v' while 'b' borrows 'v[*][*]'
int pointee_across_branch(Owners& v, bool more) {
  R& b = *v[0];
  add_or_erase(v, more);
  return b.value;
}

// rules: STN-91
// expect: not-stone: loans: changes 'v[*]' while 'b' borrows 'v[*][*]'
int pointee_across_reset(Owners& v) {
  R& b = *v[0];
  reset_first(v);
  return b.value;
}

// rules: STN-95
// expect: not-stone: loans: changes 'd' while 'it' borrows 'd[*]'
int iterator_across_helper(D& d) {
  auto it = d.begin();
  push(d);
  return *it;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
static Owners one() {
  Owners v;
  v.push_back(std::make_unique<R>(R{5}));
  return v;
}
static D many() {
  D d;
  for (int i = 0; i < 600; ++i) d.push_back(i);
  return d;
}
PORK_RUN_CASES(
    {"add", [] { Owners v; add(v); return v.size() == 1 ? 0 : 1; }},
    {"add_twice", [] { Owners v; add_twice(v); return v.size() == 2 ? 0 : 1; }},
    {"push", [] { D d; push(d); return d.size() == 1 ? 0 : 1; }},
    {"clear_and_add", [] { Owners v = one(); clear_and_add(v); return v.size() == 1 ? 0 : 1; }},
    {"add_or_erase", [] { Owners v = one(); add_or_erase(v, false); return v.empty() ? 0 : 1; }},
    {"reset_first", [] { Owners v = one(); reset_first(v); return v[0] ? 1 : 0; }},
    {"Pool::grow", [] { Pool p; p.grow(); return p.items.size() == 1 ? 0 : 1; }},
    {"pointee_across_helper", [] { Owners v = one(); return pointee_across_helper(v) == 5 ? 0 : 1; }},
    {"pointee_across_two_levels", [] { Owners v = one(); return pointee_across_two_levels(v) == 5 ? 0 : 1; }},
    {"pointee_across_method", [] { Pool p{one()}; return pointee_across_method(p) == 5 ? 0 : 1; }},
    {"reference_across_helper", [] { D d = many(); return reference_across_helper(d) == 0 ? 0 : 1; }},
    {"element_across_helper", [] { Owners v = one(); return element_across_helper(v) == 5 ? 0 : 1; }},
    {"pointee_across_clear", [] { Owners v = one(); return pointee_across_clear(v) == 5 ? 0 : 1; }},
    {"pointee_across_branch", [] { Owners v = one(); return pointee_across_branch(v, false) == 5 ? 0 : 1; }},
    {"pointee_across_reset", [] { Owners v = one(); return pointee_across_reset(v) == 5 ? 0 : 1; }},
    {"iterator_across_helper", [] { D d = many(); return iterator_across_helper(d) == 0 ? 0 : 1; }})
#endif
