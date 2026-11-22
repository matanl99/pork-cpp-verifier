// Kända luckor: idiomatisk C++ från rugby som är säker men inte Stone än.
// Förväntningarna beskriver dagens dom. När en lucka stängs ändras raden
// (och fallet flyttas till rätt korpusfil).

#include <algorithm>
#include <map>
#include <optional>
#include <variant>
#include <vector>

struct Pass { int from{0}; int to{0}; };
struct Run { int who{0}; };
using Cmd = std::variant<Run, Pass>;
struct Planned { int tick{0}; Cmd cmd; };

// 2. (Stängd av STN-79: lambda som argument, nu i corpus/71_lambda_arguments.cpp.)


// Luckor från porks egen core/Classify.cpp (självanalys, 1/7 Stone).

// 8. Elementet från map::operator[] som objekt i ett muterande anrop:
// insättningen sker före push_back, så ordningen är väldefinierad.
// rules: STN-59
// expect: not-stone: side effect inside expression
void index_append(std::map<int, std::vector<int>>& index, int key) {
  index[key].push_back(key);
}

// 9. Tilldelning till map::operator[] (insättning, sedan tilldelning av elementet).
// rules: STN-59
// expect: not-stone: side effect inside expression
void index_assign(std::map<int, int>& index, int key) {
  index[key] = 1;
}

// 10. Lokal static const som initieras konstant (aldrig ändrad) och returneras
// som referens när inget annat finns.
// rules: STN-18
// expect: not-stone: local variable 'none' has static storage
const std::vector<int>& targets_or_none(const std::map<int, std::vector<int>>& index, int key) {
  static const std::vector<int> none;
  auto it = index.find(key);
  return it == index.end() ? none : it->second;
}

// Hjälp till 11: ett lån av argumentet (STN-18, const T& från const&-parameter).
// rules: STN-18
// expect: stone: assuming
const std::vector<int>& first_targets(const std::map<int, std::vector<int>>& index, int key) {
  return index.at(key);
}

// 11. Range-for över en const T& som en egen funktion returnerar (ett lån av
// argumentet, inte en temporär).
// rules: STN-35
// expect: not-stone: range-for over a temporary
// loans: ok
int sum_targets(const std::map<int, std::vector<int>>& index, int key) {
  int sum = 0;
  for (int t : first_targets(index, key)) sum += t;
  return sum;
}

// 12. Vektor av pekare till const-element i en const&-parameter, som lever
// kortare än parametern (en lista av lån).
// rules: STN-14
// expect: not-stone: is not a value type
int count_positive(const std::vector<Pass>& passes) {
  std::vector<const Pass*> positive;
  for (const Pass& p : passes)
    if (p.to > 0) positive.push_back(&p);
  return static_cast<int>(positive.size());
}

// 13. Resultatet av map::emplace används direkt (.second): pair<iterator, bool>.
// Paret är ett lånevärde; kvar: anropet ändrar sitt objekt inne i ett uttryck.
// rules: STN-20
// expect: not-stone: side effect inside expression
// loans: ok
bool first_contract(std::map<int, int>& owners, int method, int owner) {
  return owners.emplace(method, owner).second;
}
