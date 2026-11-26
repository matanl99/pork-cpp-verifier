// En referens bunden till en klammerlista med ett element (`read({7})`,
// `const int& r = {c[0]};`) binds till elementet ([dcl.init.list]): listan är
// ett lvärde för det, eller för en temporär som elementet skapar (STN-62).
#include <vector>

// rules: STN-62
// expect: stone
int read(const int& x) { return x; }

// --- godtas -------------------------------------------------------------------

// rules: STN-62, STN-28
// expect: stone
int braced_temporary() { return read({7}); }

// rules: STN-62
// expect: stone
int braced_parameter(int y) {
  const int& r = {y};
  return r + read({y});
}

// Livstidsförlängd temporär.
// rules: STN-62, STN-32
// expect: stone
int braced_extended() {
  const int& r = {7};
  return r;
}

// --- avvisas ------------------------------------------------------------------

// rules: STN-62
// expect: not-stone: loans: changes 'v' while 'r' borrows 'v[*]'
int braced_element(std::vector<int>& v, int n) {
  const std::vector<int>& c = v;
  const int& r = {c[0]};
  for (int i = 0; i < n; ++i) v.push_back(i);
  return r;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"read", [] { return read(3) == 3 ? 0 : 1; }},
    {"braced_temporary", [] { return braced_temporary() == 7 ? 0 : 1; }},
    {"braced_parameter", [] { return braced_parameter(2) == 4 ? 0 : 1; }},
    {"braced_extended", [] { return braced_extended() == 7 ? 0 : 1; }},
    {"braced_element", [] { std::vector<int> v{5}; return braced_element(v, 100) == 5 ? 0 : 1; }})
#endif
