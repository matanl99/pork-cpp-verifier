// Referens genom en lokal iterator som returneras (STN-103): `it->second` och
// `*it` där it är en lokal iterator. Vägen börjar i iteratorn, inte i en
// parameter; om det iteratorn pekar in i lever efter anropet avgör
// lånemodellen (STN-62).
#include <map>
#include <string>
#include <vector>

// rules: STN-99, STN-103
// expect: stone: assuming
const std::vector<int>& targets_or_none(const std::map<int, std::vector<int>>& index, int key) {
  static const std::vector<int> none;
  auto it = index.find(key);
  return it == index.end() ? none : it->second;
}

// rules: STN-10, STN-103
// expect: stone: assuming
const int& first_positive(const std::vector<int>& v) {
  auto it = v.begin();
  while (it != v.end() && *it <= 0) ++it;
  return it != v.end() ? *it : v.front();
}

// Iteratorn pekar in i en lokal vektor som dör vid return.
// rules: STN-62, STN-103
// expect: not-stone: loans: returns a loan of 'v[*]', which does not outlive the function
const int& into_local() {
  std::vector<int> v{1, 2};
  auto it = v.begin();
  return *it;
}

// Iteratorn pekar in i en värdeparameter, som dör vid return.
// rules: STN-62, STN-103
// expect: not-stone: loans: returns a loan of 'v[*]', which does not outlive the function
const int& into_value_parameter(std::vector<int> v) {
  auto it = v.begin();
  return *it;
}

// rules: STN-1
// expect: stone: assuming
std::vector<int> make() { return {1, 2}; }

// Iteratorn pekar in i en temporär som dör vid semikolonet.
// rules: STN-62, STN-103
// expect: not-stone: loans: 'it' borrows a temporary
const int& into_temporary() {
  auto it = make().begin();
  return *it;
}

// Iteratorn ogiltigförklaras före return.
// rules: STN-62, STN-103
// expect: not-stone: loans: changes 'v' while 'it' borrows 'v[*]'
const int& invalidated(std::vector<int>& v) {
  auto it = v.begin();
  v.push_back(1);
  return *it;
}

// Anroparen håller resultatet medan mappen töms.
// rules: STN-62, STN-103
// expect: not-stone: loans:
int caller_clears(std::map<int, std::vector<int>>& m) {
  const std::vector<int>& r = targets_or_none(m, 1);
  m.clear();
  return static_cast<int>(r.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"targets_or_none", [] {
       std::map<int, std::vector<int>> m{{1, {2, 3}}};
       return targets_or_none(m, 1).size() == 2 && targets_or_none(m, 7).empty() ? 0 : 1;
     }},
    {"first_positive", [] {
       std::vector<int> v{-1, 0, 4, 5};
       return first_positive(v) == 4 ? 0 : 1;
     }})
#endif
