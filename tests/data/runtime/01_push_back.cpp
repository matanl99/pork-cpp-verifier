// Självtest för tests/check_runtime.py: med tests/data/override.summaries
// (push_back påstås inte ogiltigförklara) blir it_across_push Stone, och
// körningen ska larma (_GLIBCXX_DEBUG/ASan): HOLE. Utan den: caught.
#include <vector>

// expect: not-stone
int it_across_push(std::vector<int>& v) {
  auto it = v.begin();
  for (int i = 0; i < 100; ++i) v.push_back(i);
  return *it;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"it_across_push", [] { std::vector<int> v{1}; return it_across_push(v) == 1 ? 0 : 1; }})
#endif
