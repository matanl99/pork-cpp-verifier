// Intervallformen av std::ranges-algoritmerna (STN-87): ett argument med
// begin() (behållare, span) är ett intervall. Raden för iteratorformen gäller
// inte: ranges::copy(r, out) är inte (first, last), och ranges::move(r, out)
// är inte std::move(x). Ett intervall är ett eget [first, last), så en annan
// iterator in i det avvisas när algoritmen skriver.
#include <algorithm>
#include <span>
#include <vector>

// --- godtas -----------------------------------------------------------------

// rules: STN-87
// expect: stone: assuming
void copy_between(const std::vector<int>& from, std::vector<int>& to) { std::ranges::copy(from, to.begin()); }

// rules: STN-87
// expect: stone: assuming
void move_between(std::vector<int>& from, std::vector<int>& to) { std::ranges::move(from, to.begin()); }

// rules: STN-87
// expect: stone: assuming
void copy_from_span(std::vector<int>& from, std::vector<int>& to) {
  std::span<int> s(from);
  std::ranges::copy(s, to.begin());
}

// rules: STN-87
// expect: stone: assuming
void sort_range(std::vector<int>& v) { std::ranges::sort(v); }

// Två läsande intervall får vara samma objekt.
// rules: STN-87
// expect: stone: assuming
bool equal_to_itself(const std::vector<int>& a) { return std::ranges::equal(a, a); }

// rules: STN-87
// expect: stone: assuming
bool equal_ranges(const std::vector<int>& a, const std::vector<int>& b) { return std::ranges::equal(a, b); }

// rules: STN-87
// expect: stone: assuming
long first_negative(std::span<const int> s) {
  auto it = std::ranges::find_if(s, [](int x) { return x < 0; });
  return it - s.begin();
}

// Iteratorformen har kvar sin rad.
// rules: STN-87, STN-77
// expect: stone: assuming
void copy_iterators(const std::vector<int>& from, std::vector<int>& to) {
  std::ranges::copy(from.begin(), from.end(), to.begin());
}

// --- underkänns -------------------------------------------------------------

// [alg.move]: result får inte ligga i [first, last). Raden för std::move(x) gäller inte.
// rules: STN-87
// expect: not-stone: loans: operands 'v' and 'v[*]'
void move_into_itself(std::vector<int>& v) { std::ranges::move(v, v.begin() + 1); }

// [alg.copy]: (s, s.begin() + 1) är inte (first, last).
// rules: STN-87
// expect: not-stone: loans: iterator arguments 1 and 2 of 'std::ranges::__copy_fn::operator()' may point into the same object
void copy_into_itself(std::vector<int>& v) {
  std::span<int> s(v);
  std::ranges::copy(s, s.begin() + 1);
}

// rules: STN-87
// expect: not-stone: loans: iterator arguments 1 and 2 of 'std::ranges::__copy_backward_fn::operator()'
void copy_backward_into_itself(std::vector<int>& v) {
  std::span<int> s(v);
  std::ranges::copy_backward(s, s.end() - 1);
}

// rules: STN-87
// expect: not-stone: loans: iterator arguments 1 and 2 of 'std::ranges::__transform_fn::operator()'
void transform_into_itself(std::vector<int>& v) {
  std::span<int> s(v);
  std::ranges::transform(s, s.begin() + 1, [](int x) { return x + 1; });
}

// Ingen rad för intervallformen av rotate: rollerna är okända.
// rules: STN-87, STN-77
// expect: not-stone: loans: the iterator arguments of 'std::ranges::__rotate_fn::operator()' have no roles
void rotate_span(std::vector<int>& v) {
  std::span<int> s(v);
  std::ranges::rotate(s, s.begin() + 1);
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"copy_between", [] { std::vector<int> a{1, 2}, b{0, 0, 0}; copy_between(a, b); return b == std::vector<int>{1, 2, 0} ? 0 : 1; }},
    {"move_between", [] { std::vector<int> a{1, 2}, b{0, 0}; move_between(a, b); return b == std::vector<int>{1, 2} ? 0 : 1; }},
    {"copy_from_span", [] { std::vector<int> a{3}, b{0}; copy_from_span(a, b); return b[0] == 3 ? 0 : 1; }},
    {"sort_range", [] { std::vector<int> v{2, 1}; sort_range(v); return v[0] == 1 ? 0 : 1; }},
    {"equal_to_itself", [] { return equal_to_itself({1, 2}) ? 0 : 1; }},
    {"equal_ranges", [] { return equal_ranges({1}, {2}) ? 1 : 0; }},
    {"first_negative", [] { std::vector<int> v{1, -2}; return first_negative(v) == 1 ? 0 : 1; }},
    {"copy_iterators", [] { std::vector<int> a{4}, b{0}; copy_iterators(a, b); return b[0] == 4 ? 0 : 1; }})
#endif
