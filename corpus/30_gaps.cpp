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

// 10. (Stängd av STN-99: statisk lokal konstant, nu i corpus/148_static_sentinel.cpp.)

// 11. (Stängd av STN-98: range-for över en const T& från ett anrop, nu i
// corpus/147_call_results.cpp.)

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

// 13. (Stängd av STN-97: fält i resultatet av det yttersta anropet, nu i
// corpus/147_call_results.cpp.)
