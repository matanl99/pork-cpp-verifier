// Resultatet av ett anrop med sidoeffekt, använt direkt.
//
// STN-97: ett fält i det temporära resultatet av det yttersta anropet
// (`m.emplace(k, v).second`) får läsas där anropet självt får stå (hela
// returvärdet, initieraren, högerledet eller if-villkoret). Anropet körs en gång,
// och läsningen av fältet i temporären är det enda som följer.
//
// STN-98: range-for över en `const T&` som ett anrop returnerar. Anropet är
// ingen temporär; om det lånade lever genom loopen avgör lånemodellen (STN-62).
#include <map>
#include <vector>

// rules: STN-97
// expect: stone: assuming
bool first_contract(std::map<int, int>& owners, int method, int owner) {
  return owners.emplace(method, owner).second;
}

// rules: STN-97
// expect: stone: assuming
int inserted_count(std::map<int, int>& owners, int a, int b) {
  bool first = owners.emplace(a, 1).second;
  int n = first ? 1 : 0;
  if (owners.emplace(b, 2).second) ++n;
  return n;
}

struct Step { int before; int after; };

// rules: STN-21
// expect: stone
Step bump(int& x) {
  int b = x;
  ++x;
  return Step{b, x};
}

// rules: STN-97
// expect: stone
int bumped(int& x) {
  return bump(x).after;
}

// Inte det yttersta ledet: anropet ändrar sitt objekt inne i ett uttryck.
// rules: STN-21, STN-97
// expect: not-stone: side effect inside expression
bool inserted_and(std::map<int, int>& owners, int method, bool also) {
  return owners.emplace(method, 0).second && also;
}

// Iteratorn ur resultatet är ett lån av mappen; clear() ogiltigförklarar den.
// Kopian av fältet (iteratorns kopiekonstruktor) är inget fält som läses, så
// STN-97 gäller inte, och anropet avvisas redan som sidoeffekt i ett uttryck.
// rules: STN-21, STN-97
// expect: not-stone: side effect inside expression
int stale_iterator(std::map<int, int>& owners, int method) {
  auto it = owners.emplace(method, 0).first;
  owners.clear();
  return it->second;
}

// rules: STN-18
// expect: stone: assuming
const std::vector<int>& first_targets(const std::map<int, std::vector<int>>& index, int key) {
  return index.at(key);
}

// rules: STN-35, STN-98
// expect: stone: assuming
int sum_targets(const std::map<int, std::vector<int>>& index, int key) {
  int sum = 0;
  for (int t : first_targets(index, key)) sum += t;
  return sum;
}

// rules: STN-35, STN-98
// expect: stone: assuming
int sum_at(const std::map<int, std::vector<int>>& index, int key) {
  int sum = 0;
  for (const int& t : index.at(key)) sum += t;
  return sum;
}

// rules: STN-18
// expect: stone: assuming
const std::vector<int>& items(const std::vector<int>& v) { return v; }

// rules: STN-1
// expect: stone: assuming
std::vector<int> pair_of(int key) {
  std::vector<int> v;
  v.push_back(key);
  v.push_back(1);
  return v;
}

// Vektorn är en temporär i intervallet. Från C++23 (P2718, [stmt.ranged]) lever
// den till loopens slut; under C++20 dör den när intervallet har bundits, och
// då avvisar lånemodellen fallet.
// rules: STN-62, STN-98
// expect: stone: assuming
int sum_of_temporary(int key) {
  int sum = 0;
  for (int t : items(pair_of(key))) sum += t;
  return sum;
}

// rules: STN-62, STN-98
// expect: not-stone: loans:
int grow_while_iterating(std::vector<int>& v) {
  int sum = 0;
  for (int t : items(v)) {
    sum += t;
    v.push_back(t);
  }
  return sum;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"first_contract", [] {
       std::map<int, int> m;
       return first_contract(m, 1, 2) && !first_contract(m, 1, 3) && m.at(1) == 2 ? 0 : 1;
     }},
    {"inserted_count", [] {
       std::map<int, int> m{{2, 0}};
       return inserted_count(m, 1, 2) == 1 ? 0 : 1;
     }},
    {"bumped", [] {
       int x = 4;
       return bumped(x) == 5 && x == 5 ? 0 : 1;
     }},
    {"sum_targets", [] {
       std::map<int, std::vector<int>> m{{1, {2, 3}}};
       return sum_targets(m, 1) == 5 ? 0 : 1;
     }},
    {"sum_of_temporary", [] { return sum_of_temporary(4) == 5 ? 0 : 1; }},
    {"sum_at", [] {
       std::map<int, std::vector<int>> m{{1, {2, 3}}};
       return sum_at(m, 1) == 5 ? 0 : 1;
     }})
#endif
