// Metodanrop genom operator-> (STN-62): i `o->at(0)` är objektet en pekare
// till behållaren, och resultatet lånar behållarens ägda lagring (`o[*][*]`),
// precis som `(*o).at(0)`. En senare `o->push_back` avslutar lånet. Samma
// sak genom en iterator (`it->at(0)`) och en std::unique_ptr (`p->front()`).
#include <memory>
#include <optional>
#include <vector>

using Opt = std::optional<std::vector<int>>;
using Rows = std::vector<std::vector<int>>;

// --- avvisas ------------------------------------------------------------------

// rules: STN-62
// expect: not-stone: loans: changes 'o[*]' while 'r' borrows 'o[*][*]'
int at_through_arrow(Opt& o) {
  int& r = o->at(0);
  o->push_back(1);
  return r;
}

// rules: STN-62
// expect: not-stone: loans: changes 'o[*]' while 'r' borrows 'o[*][*]'
int front_through_arrow(Opt& o) {
  int& r = o->front();
  (*o).push_back(1);
  return r;
}

// rules: STN-62
// expect: not-stone: loans: changes 'v[*]' while 'r' borrows 'v[*][*]'
int at_through_iterator(Rows& v) {
  auto it = v.begin();
  int& r = it->at(0);
  v[0].push_back(1);
  return r;
}

// rules: STN-62
// expect: not-stone: loans: changes 'p[*]' while 'r' borrows 'p[*][*]'
int front_through_unique_ptr(std::unique_ptr<std::vector<int>>& p) {
  int& r = p->front();
  p->push_back(1);
  return r;
}

// --- godtas -------------------------------------------------------------------

// rules: STN-62
// expect: stone: assuming
int borrow_after_insert(Opt& o) {
  o->push_back(1);
  int& r = o->at(0);
  return r;
}

// rules: STN-62
// expect: stone: assuming
int size_before_insert(Opt& o) {
  auto n = o->size();
  o->push_back(1);
  return static_cast<int>(n);
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"at_through_arrow", [] { Opt o{{1}}; return at_through_arrow(o) == 1 ? 0 : 1; }},
    {"front_through_arrow", [] { Opt o{{1}}; return front_through_arrow(o) == 1 ? 0 : 1; }},
    {"at_through_iterator", [] { Rows v{{1}}; return at_through_iterator(v) == 1 ? 0 : 1; }},
    {"front_through_unique_ptr", [] {
       auto p = std::make_unique<std::vector<int>>(1, 1);
       return front_through_unique_ptr(p) == 1 ? 0 : 1;
     }},
    {"borrow_after_insert", [] { Opt o{{2}}; return borrow_after_insert(o) == 2 ? 0 : 1; }},
    {"size_before_insert", [] { Opt o{{2}}; return size_before_insert(o) == 1 ? 0 : 1; }})
#endif
