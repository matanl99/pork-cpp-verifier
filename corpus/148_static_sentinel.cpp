// En statisk lokal konstant av värdetyp med konstant initiering (STN-99): en
// tom behållare som returneras som const T& när inget annat finns. Konstant
// initiering betyder att ingen kod körs vid första anropet, och const att
// värdet aldrig ändras; med std::vector och std::string kan den bara vara tom.
#include <map>
#include <string>
#include <vector>

// rules: STN-99
// expect: stone: assuming
const std::vector<int>& targets_or_none(const std::map<int, std::vector<int>>& index, int key) {
  static const std::vector<int> none;
  return index.contains(key) ? index.at(key) : none;
}

// rules: STN-99
// expect: stone: assuming
const std::string& name_or_empty(const std::map<int, std::string>& names, int key) {
  static const std::string empty;
  if (!names.contains(key)) return empty;
  return names.at(key);
}

// rules: STN-99
// expect: stone: assuming
int none_size() {
  static const std::vector<int> none;
  return static_cast<int>(none.size());
}

// Inte const: värdet kan ändras mellan anrop.
// rules: STN-99
// expect: not-stone: local variable 'cache' has static storage
int cached(int x) {
  static std::vector<int> cache;
  cache.push_back(x);
  return static_cast<int>(cache.size());
}

// Inte konstant initierad: allokeringen körs vid första anropet.
// rules: STN-99
// expect: not-stone: local variable 'primes' has static storage
int first_prime() {
  static const std::vector<int> primes{2, 3, 5};
  return primes[0];
}

// Ett mutable-fält kan ändras genom en const-metod: värdet är inte konstant.
struct Counter {
  mutable int n = 0;
  // rules: STN-2
  // expect: not-stone: member of non-object type Counter
  int next() const { return ++n; }
};

// rules: STN-99
// expect: not-stone: local variable 'c' has static storage
int counted() {
  static const Counter c;
  return c.next();
}

// const bort med const_cast: skrivningen gör konstanten föränderlig.
// rules: STN-99
// expect: not-stone: unsupported expression: CXXConstCastExpr
int grown() {
  static const std::vector<int> none;
  const_cast<std::vector<int>&>(none).push_back(1);
  return static_cast<int>(none.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"targets_or_none", [] {
       std::map<int, std::vector<int>> m{{1, {2, 3}}};
       return targets_or_none(m, 1).size() == 2 && targets_or_none(m, 7).empty() ? 0 : 1;
     }},
    {"name_or_empty", [] {
       std::map<int, std::string> m{{1, "a"}};
       return name_or_empty(m, 1) == "a" && name_or_empty(m, 2).empty() ? 0 : 1;
     }},
    {"none_size", [] { return none_size(); }})
#endif
