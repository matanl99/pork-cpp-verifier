// std::get<I>(t) som accessor (STN-37, STN-59): ett T& in i argumentet, som
// t.first. `std::get<0>(p) = x;` är en skrivning till p (en väg), inte ett anrop
// med sidoeffekt inuti ett uttryck, och för ett std::pair är målet fältet
// first eller second (BRZ-2). Lånemodellen ser att std::get<0>(p) och p.first
// är samma objekt.

#include <pork/annotations.hpp>
#include <tuple>
#include <utility>

struct S {
  int x;
  int y;
};

// rules: STN-12
// expect: stone
int bump(S& s, const int& v) {
  s.x = 0;
  return v;
}

// rules: STN-37, BRZ-2
// expect: bronze: assuming std::get, type std::pair
void set_first(std::pair<int, int>& out, int s) {
  PORK_DEPENDS(out.first, s);
  std::get<0>(out) = s;
}

// rules: STN-37, BRZ-2, BRZ-6
// expect: stone: not bronze: out.second depends on s, which PORK_DEPENDS does not allow
// expect-violation
void set_second(std::pair<int, int>& out, int s) {
  PORK_DEPENDS(out.second, out.second);
  std::get<1>(out) = s;
}

// rules: STN-37
// expect: stone: assuming std::get, type std::pair
int read_second(const std::pair<int, int>& p) { return std::get<1>(p) + 1; }

// rules: STN-37
// expect: stone: assuming std::get, type std::tuple
void set_tuple(std::tuple<int, double>& t, int s) { std::get<0>(t) = s; }

// rules: STN-37, STN-62
// expect: not-stone: loans: operands 'p.first' and 'p[*].x' of the call to 'bump' may be the same object and one is changed (line 49)
int same_object(std::pair<S, int>& p) {
  int r = bump(p.first, std::get<0>(p).x);
  return r;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"set_first", [] { std::pair<int, int> p{1, 2}; set_first(p, 5); return p.first == 5 && p.second == 2 ? 0 : 1; }},
    {"read_second", [] { return read_second({1, 2}) == 3 ? 0 : 1; }},
    {"set_tuple", [] { std::tuple<int, double> t{1, 2.0}; set_tuple(t, 4); return std::get<0>(t) == 4 ? 0 : 1; }})
#endif
