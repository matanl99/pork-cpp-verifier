// En referens till ett fält i ett element: `int& v = vec[1].age_;` (STN-37,
// STN-31). Accessorn i roten är en skrivning till behållaren (en väg), som
// `vec[1].age_ = 3;` och som omvägen `A& item = vec[1]; int& v = item.age_;`.
// Lånet följs av lånemodellen (STN-62).

#include <vector>

struct A {
  int age_;
  int id_;
};

struct Team {
  std::vector<A> members;
};

// rules: STN-37, STN-31
// expect: stone: assuming std::vector::operator[], std::vector::size, type std::allocator, type std::vector
int set_age(std::vector<A>& vec) {
  if (vec.size() < 2) return 0;
  int& v = vec[1].age_;
  v = 3;
  return v;
}

// rules: STN-37, STN-31
// expect: stone: assuming std::vector::empty, std::vector::operator[], type std::allocator, type std::vector
int bump_id(Team& t) {
  if (t.members.empty()) return 0;
  int& v = t.members[0].id_;
  if (v < 1000) v += 1;
  return v;
}

// rules: STN-37, STN-62
// expect: not-stone: loans: changes 'vec' while 'v' borrows 'vec[*].age_' and is used later (line 40)
int bad_grow(std::vector<A>& vec) {
  if (vec.size() < 2) return 0;
  int& v = vec[1].age_;
  vec.push_back(A{1, 2});
  v = 3;
  return v;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"set_age", [] { std::vector<A> v{{1, 2}, {3, 4}}; return set_age(v) == 3 && v[1].age_ == 3 ? 0 : 1; }},
    {"bump_id", [] { Team t{{{1, 7}}}; return bump_id(t) == 8 && t.members[0].id_ == 8 ? 0 : 1; }})
#endif
