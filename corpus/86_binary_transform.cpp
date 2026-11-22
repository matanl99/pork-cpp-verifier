// Binär std::ranges::transform(first1, last1, first2, last2, result, op) har
// två intervall (STN-77, summaries/std.txt: transform med fem
// iteratorparametrar). Förut fick den bara det första: [in.right.begin(),
// in.unrelated.end()) godkändes (stilexempel 05). För
// std::array är iteratorerna pekare: ingen körning ser felet.
#include <algorithm>
#include <array>
#include <vector>

using Row = std::array<int, 3>;
struct Grid { Row left; Row right; Row unrelated; };

// rules: STN-77
// expect: stone: assuming
void ok_second_end(const Grid& in, Row& out) {
  auto dest = out.begin();
  std::ranges::transform(in.left.begin(), in.left.end(), in.right.begin(), in.right.end(), dest,
                         [](int a, int b) { return a + b; });
}

// rules: STN-77
// expect: not-stone: 'in.right[*]' to 'in.unrelated[*]', which are not known to be the same object
void bad_second_end(const Grid& in, Row& out) {
  auto dest = out.begin();
  std::ranges::transform(in.left.begin(), in.left.end(), in.right.begin(), in.unrelated.end(), dest,
                         [](int a, int b) { return a + b; });
}

// rules: STN-77
// expect: not-stone: which are not known to be the same object
void bad_second_end_vector(const std::vector<int>& a, const std::vector<int>& b, const std::vector<int>& c,
                           std::vector<int>& out) {
  auto dest = out.begin();
  std::ranges::transform(a.begin(), a.end(), b.begin(), c.end(), dest, [](int x, int y) { return x + y; });
}

// std::transform(first1, last1, first2, result, op): first2 är ensam.
// rules: STN-77
// expect: stone: assuming
void classic_binary(const std::vector<int>& a, const std::vector<int>& b, std::vector<int>& out) {
  auto dest = out.begin();
  std::transform(a.begin(), a.end(), b.begin(), dest, [](int x, int y) { return x + y; });
}

// constexpr-oraklet: quiet för bad_second_end. Två olika intervall bryter mot
// algoritmens förvillkor, men loopen jämför bara pekarna med == (definierat)
// och stannar vid last1: inget fel i kärnspråket för kompilatorn att se.
#ifdef PORK_CONSTEXPR
#include <pork/constexpr.hpp>
PORK_CONSTEXPR_CASE("bad_second_end", Grid in{{1, 2, 3}, {4, 5, 6}, {8, 8, 8}}; Row out{}; bad_second_end(in, out); return true;)
PORK_CONSTEXPR_CASE("ok_second_end", Grid in{{1, 2, 3}, {4, 5, 6}, {8, 8, 8}}; Row out{}; ok_second_end(in, out);
                    return out == Row{5, 7, 9};)
#endif

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"ok_second_end", [] { Grid in{{1, 2, 3}, {4, 5, 6}, {8, 8, 8}}; Row out{}; ok_second_end(in, out); return out == Row{5, 7, 9} ? 0 : 1; }},
    {"classic_binary", [] { std::vector<int> a{1, 2}, b{3, 4}, out(2); classic_binary(a, b, out); return out[1] == 6 ? 0 : 1; }},
    {"bad_second_end_vector", [] { std::vector<int> a{1, 2}, b{3, 4}, c{5, 6}, out(2); bad_second_end_vector(a, b, c, out); return 0; }})
#endif
