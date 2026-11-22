// Att ett icke-const std-anrop bara läser kommer från summaries/std.txt
// (reads/access), inte från att det finns en const-överlagring (STN-37,
// STN-78). Här ersätter 72_summary_reads.summaries raderna för front och end.
#include <algorithm>
#include <vector>

// Med std.txt: Stone. Utan raden kan v.front() skriva v.
// rules: STN-37
// expect: not-stone: side effect inside expression
int first_plus(std::vector<int>& v) { return v.empty() ? 0 : v.front() + 1; }

// En const-metod läser alltid.
// rules: STN-37
// expect: stone: assuming
int first_plus_const(const std::vector<int>& v) { return v.empty() ? 0 : v.front() + 1; }

// rules: STN-37
// expect: stone: assuming
int last_plus(std::vector<int>& v) { return v.empty() ? 0 : v.back() + 1; }

// rules: STN-78
// expect: not-stone: side effect inside expression
void sort_all(std::vector<int>& v) { std::sort(v.begin(), v.end()); }
