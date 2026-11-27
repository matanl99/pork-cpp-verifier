// En std-konstruktor med forwarding-referenser (`pair(U1&&, U2&&)`) får ett
// lvärde som `int&`. Är argumentet av aritmetisk typ eller enum-typ kopierar
// konstruktorn bara värdet, och ingen användarkod kan nås: lånemodellen räknar
// det som en läsning (STN-25, STN-62), så samma variabel får stå två gånger.
// Andra typer kan nå användarkod som skriver (en konvertering, T(U&)) och
// räknas som anrop som kan skriva argumentet.
#include <tuple>
#include <utility>
#include <vector>

enum class Color { red, green };

// --- godtas -------------------------------------------------------------------

// rules: STN-25, STN-62
// expect: stone: assuming
int pair_same_lvalue(int n) {
  std::pair<int, int> p{n, n};
  return p.first + p.second;
}

// rules: STN-25, STN-62
// expect: stone: assuming
int pair_same_field(std::pair<int, int>& q) {
  std::pair<int, int> p{q.first, q.first};
  return p.second;
}

// rules: STN-25, STN-62
// expect: stone: assuming
int tuple_same_lvalue(int n, double d) {
  std::tuple<int, int, double> t{n, n, d};
  return std::get<0>(t) + std::get<1>(t);
}

// rules: STN-25, STN-62
// expect: stone: assuming
int pair_same_enum(Color c) {
  std::pair<Color, Color> p{c, c};
  return p.first == p.second ? 1 : 0;
}

// --- avvisas ------------------------------------------------------------------

// En klasstyp: kopieringen går genom konstruktorer som analysen inte räknar
// som läsning här.
// rules: STN-25, STN-62
// expect: not-stone: loans: operands 'v' and 'v'
int pair_same_vector(std::vector<int>& v) {
  std::pair<std::vector<int>, std::vector<int>> p{v, v};
  return static_cast<int>(p.first.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"pair_same_lvalue", [] { return pair_same_lvalue(2) == 4 ? 0 : 1; }},
    {"pair_same_field", [] { std::pair<int, int> q{3, 4}; return pair_same_field(q) == 3 ? 0 : 1; }},
    {"tuple_same_lvalue", [] { return tuple_same_lvalue(2, 1.0) == 4 ? 0 : 1; }},
    {"pair_same_enum", [] { return pair_same_enum(Color::green) == 1 ? 0 : 1; }},
    {"pair_same_vector", [] { std::vector<int> v{1}; return pair_same_vector(v) == 1 ? 0 : 1; }})
#endif
