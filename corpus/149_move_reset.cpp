// Flytt av en väg, del två (STN-100, STN-101).
//
// STN-100: i samma fullständiga uttryck som std::move(p.namn) får en väg som
// inte överlappar den flyttade förekomma (p.stad). Ordningen mellan operanderna
// är ospecificerad, men syskonet läses aldrig av flytten. Hela p, en väg som
// rymmer p.namn eller p.namn själv avvisas som förut.
//
// STN-101: ett anrop som std.txt märker `resets` (clear: [sequence.reqmts],
// [associative.reqmts.general], [string.modifiers] a.clear(), eftervillkor
// a.empty(); assign: [string.assign], [sequence.reqmts]) ger den flyttade
// vägen ett bestämt värde, som en hel tilldelning. Argumenten granskas som ett
// högerled: når de den flyttade vägen är det en användning.
#include <string>
#include <utility>
#include <vector>

struct Person {
  std::string namn;
  std::string stad;
  int alder;
};

// rules: STN-96, STN-100
// expect: stone: assuming
int sibling_in_same_expression() {
  Person p{"Anna", "Lund", 30};
  std::string s = std::move(p.namn) + p.stad;
  return int(s.size()) + p.alder;
}

// rules: STN-96, STN-100
// expect: not-stone: 'p' is used in the same call as std::move(p.namn)
int same_path_in_same_expression() {
  Person p{"Anna", "Lund", 30};
  std::string s = std::move(p.namn) + p.namn;
  return int(s.size());
}

// rules: STN-12
// expect: stone: assuming
int count(const Person& p) { return int(p.namn.size() + p.stad.size()); }

// rules: STN-96, STN-100
// expect: not-stone: 'p' is used in the same expression as std::move(p.namn)
int whole_in_same_expression() {
  Person p{"Anna", "Lund", 30};
  return int(std::move(p.namn).size()) + count(p);
}

// rules: STN-96, STN-101
// expect: stone: assuming
int cleared_after_move() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  p.namn.clear();
  return int(n.size() + p.namn.size());
}

// rules: STN-96, STN-101
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int read_before_clear() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  int k = int(p.namn.size());
  p.namn.clear();
  return k + int(n.size());
}

// rules: STN-57, STN-101
// expect: stone: assuming
int cleared_vector(std::vector<int> v) {
  std::vector<int> w = std::move(v);
  v.clear();
  v.push_back(1);
  return int(v.size() + w.size());
}

// Andra anrop än clear läser eller förutsätter innehållet.
// rules: STN-96, STN-101
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int resized_after_move() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  p.namn.resize(2);
  return int(n.size() + p.namn.size());
}

// clear bara i en gren: på den andra vägen läses den flyttade vägen.
// rules: STN-96, STN-101
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int cleared_in_branch(bool c) {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  if (c) p.namn.clear();
  return int(n.size() + p.namn.size());
}

// rules: STN-96, STN-101
// expect: stone: assuming
int assigned_after_move() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  p.namn.assign("Bo");
  return int(n.size() + p.namn.size());
}

// rules: STN-96, STN-101
// expect: stone: assuming
int assigned_from_sibling() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  p.namn.assign(p.stad);
  return int(n.size() + p.namn.size());
}

// rules: STN-96, STN-101
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int assigned_from_moved() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  p.namn.assign(p.namn);
  return int(n.size() + p.namn.size());
}

// rules: STN-57, STN-101
// expect: stone: assuming
int vector_assigned(std::vector<int> v) {
  std::vector<int> w = std::move(v);
  v.assign(3, 7);
  return int(v.size() + w.size());
}

// rules: STN-57, STN-101
// expect: not-stone: 'v' is used after std::move(v)
int vector_assigned_from_itself(std::vector<int> v) {
  std::vector<int> w = std::move(v);
  v.assign(v.begin(), v.end());
  return int(v.size() + w.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"sibling_in_same_expression", [] { return sibling_in_same_expression() == 38 ? 0 : 1; }},
    {"cleared_after_move", [] { return cleared_after_move() == 4 ? 0 : 1; }},
    {"cleared_vector", [] { return cleared_vector({1, 2}) == 3 ? 0 : 1; }},
    {"assigned_after_move", [] { return assigned_after_move() == 6 ? 0 : 1; }},
    {"assigned_from_sibling", [] { return assigned_from_sibling() == 8 ? 0 : 1; }},
    {"vector_assigned", [] { return vector_assigned({1, 2}) == 5 ? 0 : 1; }})
#endif
