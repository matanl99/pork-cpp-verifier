// Två precisionsfel från stilexemplen, båda för strikta:
//  - 01: std::ranges::sort(v.begin(), v.end()) som sats avvisades
//    (`side effect inside expression` för begin), medan std::sort och samma
//    anrop med lokala iteratorer godkändes. En niebloid är en fri funktion
//    för STN-78 som för lånemodellen (isNiebloid).
//  - 10: `token == "null"` på std::string_view avvisades som en jämförelse
//    av iteratorer i olika objekt. Kravet på samma objekt gäller positioner
//    (iteratorer, pekare); en vy med begin() är ett intervall och `==`
//    jämför innehållet.
#include <algorithm>
#include <string_view>
#include <vector>

// rules: STN-78
// expect: stone: assuming
void sort_direct(std::vector<int>& row) { std::ranges::sort(row.begin(), row.end()); }

// rules: STN-78, STN-77
// expect: not-stone: which are not known to be the same object
void bad_sort_mixed(std::vector<int>& a, std::vector<int>& b) { std::ranges::sort(a.begin(), b.end()); }

// rules: STN-78
// expect: not-stone: side effect inside expression
int bad_sort_in_expression(std::vector<int>& a) { return (std::ranges::sort(a.begin(), a.end()), a[0]); }

// rules: STN-62
// expect: stone: assuming
bool is_yaml_null(std::string_view token) {
  return token.empty() || token == "~" || token == "null" || token == "Null" || token == "NULL";
}

// rules: STN-62
// expect: not-stone: which are not known to be the same object
bool bad_iterators_of_two(const std::vector<int>& a, const std::vector<int>& b) { return a.begin() == b.end(); }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"sort_direct", [] { std::vector<int> v{3, 1, 2}; sort_direct(v); return v == std::vector<int>{1, 2, 3} ? 0 : 1; }},
    {"is_yaml_null", [] { return is_yaml_null("") && is_yaml_null("Null") && !is_yaml_null("NuLL") && !is_yaml_null("nullx") ? 0 : 1; }},
    {"bad_iterators_of_two", [] { std::vector<int> a{1}, b{2}; return bad_iterators_of_two(a, b) ? 1 : 0; }})
#endif
