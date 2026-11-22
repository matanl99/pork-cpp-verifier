// Adversariell granskning (I2): std::begin(v) ska ge samma dom som
// v.begin() ([iterator.range]: std::begin(c) returnerar c.begin()). Raden
// `free begin end ... : access` i summaries/std.txt; lånemodellen följer
// iteratorerna som för metoden.
#include <algorithm>
#include <iterator>
#include <vector>

// rules: STN-78
// expect: stone: assuming
void member_begin(std::vector<int>& v) { std::sort(v.begin(), v.end()); }

// rules: STN-78
// expect: stone: assuming
void free_begin(std::vector<int>& v) { std::sort(std::begin(v), std::end(v)); }

// rules: STN-77
// expect: not-stone: not known to be the same object
void free_begin_two(std::vector<int>& a, std::vector<int>& b) { std::sort(std::begin(a), std::end(b)); }

// rules: STN-62
// expect: not-stone: not known to be the same object
bool free_compare_two(std::vector<int>& a, std::vector<int>& b) { return std::begin(a) == std::end(b); }

// rules: STN-62
// expect: not-stone: while
int free_begin_invalidated(std::vector<int>& v) {
  auto it = std::begin(v);
  v.push_back(1);
  return *it;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"member_begin", [] { std::vector<int> v{3, 1, 2}; member_begin(v); return v[0] == 1 ? 0 : 1; }},
    {"free_begin", [] { std::vector<int> v{3, 1, 2}; free_begin(v); return v[0] == 1 ? 0 : 1; }},
    {"free_begin_two", [] { std::vector<int> a{3, 2}, b{1, 0}; free_begin_two(a, b); return 0; }},
    {"free_compare_two", [] { std::vector<int> a{1}, b{2}; return free_compare_two(a, b) ? 1 : 0; }},
    {"free_begin_invalidated", [] { std::vector<int> v{1}; return free_begin_invalidated(v) == 1 ? 0 : 1; }})
#endif
