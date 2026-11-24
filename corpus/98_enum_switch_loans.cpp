// Kod efter en switch där varje fall för en enum returnerar nås när enumen
// har ett annat värde. Lånemodellen (STN-62) ska granska den.

#include <vector>

enum class Scoped : int { A, B };

// rules: STN-13, STN-62
// expect: not-stone: loans: changes 'v' while 'r' borrows 'v[*]' and is used later (line 16)
int bad_after(Scoped s, std::vector<int>& v) {
  const int& r = v[0];
  switch (s) {
    case Scoped::A: return 1;
    case Scoped::B: return 2;
  }
  v.push_back(3);
  return r;
}

// rules: STN-13, STN-62
// expect: stone: assuming std::vector::operator[], std::vector::push_back, type std::allocator, type std::vector
int ok_after(Scoped s, std::vector<int>& v) {
  switch (s) {
    case Scoped::A: return 1;
    case Scoped::B: return 2;
  }
  v.push_back(3);
  return v[0];
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_after", [] { std::vector<int> v{1}; return bad_after(static_cast<Scoped>(7), v) == 1 ? 0 : 0; }},
    {"ok_after", [] { std::vector<int> v{1}; return ok_after(static_cast<Scoped>(7), v) == 1 ? 0 : 1; }})
#endif
