// T& in i *this med `c ? a : b` (STN-76): bara en gren körs, efter villkoret,
// så ett anrop med sidoeffekt som är hela grenen godtas som när det är hela
// returvärdet (`return v[i];`). Anroparens lån följs som vanligt.

#include <vector>

struct S {
  std::vector<int> v;
  int x;

  // rules: STN-76
  // expect: stone: assuming std::vector::operator[], type std::allocator, type std::vector
  int& pick(bool c) { return c ? x : v[0]; }

  // rules: STN-76
  // expect: stone: assuming std::vector::push_back, type std::allocator, type std::vector
  void grow() {
    v.push_back(1);
    v.push_back(2);
  }
};

// rules: STN-76, STN-62
// expect: not-stone: loans: changes 's.v' while 'r' borrows 's.v[*]' and is used later (line 27)
int bad_keep(S& s) {
  int& r = s.pick(false);
  s.grow();
  r = 5;
  return r;
}

// rules: STN-76
// expect: stone: assuming std::vector::operator[], type std::allocator, type std::vector
int ok_use(S& s) {
  int& r = s.pick(false);
  r = 5;
  return r;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"S::pick", [] { S s{{1}, 2}; s.pick(false) = 7; s.pick(true) = 8; return s.v[0] == 7 && s.x == 8 ? 0 : 1; }},
    {"ok_use", [] { S s{{1}, 2}; return ok_use(s) == 5 && s.v[0] == 5 ? 0 : 1; }})
#endif
