#include <map>
#include <set>
#include <utility>

// rules: STN-58
// expect: stone: assuming std::map::end
int lookup(const std::map<int, int>& m, int key) {
  if (auto it = m.find(key); it != m.end()) return it->second;
  return 0;
}

// rules: STN-58
// expect: stone: assuming std::set::end
int lookup(const std::set<int>& s, int key) {
  auto it = s.find(key);
  return s.end() == it ? 0 : *it;
}

// rules: STN-62
// expect: not-stone: loans: compares 'a[*]' with 'b[*]', which are not known to be the same object
bool wrong_end(const std::set<int>& a, const std::set<int>& b, int key) {
  auto it = a.find(key);
  return it == b.end();
}

// rules: STN-62
// expect: not-stone: loans: changes 'm' while 'it' borrows 'm[*]'
int invalidated(std::map<int, int>& m, int key) {
  const auto& view = m;
  auto it = view.find(key);
  m.clear();
  return it != view.end() ? it->second : 0;
}

// rules: STN-58
// expect: stone: assuming std::map::clear
int scope_ends(std::map<int, int>& m, int key) {
  const auto& view = m;
  int result = 0;
  if (auto it = view.find(key); it != view.end()) result = it->second;
  m.clear();
  return result;
}

// rules: STN-58
// expect: stone: assuming
// loans: ok
int copied(const std::set<int>& s, int key) {
  auto it = s.find(key);
  auto copy = it;
  return copy == s.end() ? 0 : *copy;
}

// rules: STN-57
// expect: not-stone: 'view' is used after std::move(m)
int moved_container(std::map<int, int> m, int key) {
  const auto& view = m;
  auto it = view.find(key);
  auto moved = std::move(m);
  return it != view.end() ? it->second : 0;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"lookup@7", [] { std::map<int, int> m{{1, 5}}; return lookup(m, 1) == 5 && lookup(m, 2) == 0 ? 0 : 1; }},
    {"lookup@14", [] { std::set<int> s{4}; return lookup(s, 4) == 4 && lookup(s, 1) == 0 ? 0 : 1; }},
    {"scope_ends", [] { std::map<int, int> m{{1, 5}}; return scope_ends(m, 1) == 5 && m.empty() ? 0 : 1; }},
    {"copied", [] { std::set<int> s{4}; return copied(s, 4) == 4 ? 0 : 1; }})
#endif
