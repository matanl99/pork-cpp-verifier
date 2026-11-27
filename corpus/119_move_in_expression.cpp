// std::move inuti ett större uttryck (STN-57, STN-72): ordningen mellan
// operanderna och mellan argumenten är ospecificerad, så källordningen och
// clangs CFG säger inte vad som körs först. Roten (för en flyttkälla i
// T(T&&)/operator=(T&&): en väg som överlappar den flyttade) får inte nämnas
// någon annanstans i samma fullständiga uttryck.
#include <memory>
#include <utility>
#include <vector>

// rules: STN-57
// expect: stone: assuming
int take(std::unique_ptr<int> p) { return p ? *p : -1; }

// rules: STN-57
// expect: stone: assuming
int count(std::vector<int> v) { return static_cast<int>(v.size()); }

// rules: STN-57
// expect: stone
int id(int x) { return x; }

// rules: STN-57
// expect: stone
int add(int a, int b) { return a + b; }

struct Pair {
  std::vector<int> a;
  int n = 0;

  Pair() = default;

  // Olika fält i samma uttryck: o.n överlappar inte o.a.
  // rules: STN-72, STN-57
  // expect: stone: assuming
  Pair& operator=(Pair&& o) {
    n = o.n + count(std::move(o.a));
    return *this;
  }
};

struct Twice {
  std::vector<int> a;
  int n = 0;

  Twice() = default;

  // o.a.size() kan räknas efter flytten.
  // rules: STN-72, STN-57
  // expect: not-stone: 'o.a' is used in the same expression as std::move(o.a)
  Twice& operator=(Twice&& o) {
    n = static_cast<int>(o.a.size()) + count(std::move(o.a));
    return *this;
  }
};

// --- godtas -----------------------------------------------------------------

// rules: STN-57
// expect: stone: assuming
int separate_statements(std::unique_ptr<int> p) {
  int n = take(std::move(p));
  return n;
}

// rules: STN-57
// expect: stone: assuming
int only_the_move(std::unique_ptr<int> p) { return add(1, id(take(std::move(p)))); }

// rules: STN-57
// expect: stone: assuming
int other_variable(std::vector<int> x, std::vector<int> y) {
  return static_cast<int>(y.size()) + count(std::move(x));
}

// --- underkänns -------------------------------------------------------------

// g++ räknar add:s argument från höger: *p avrefererar en tom pekare.
// rules: STN-57
// expect: not-stone: 'p' is used in the same expression as std::move(p)
int nested_args(std::unique_ptr<int> p) { return add(*p, id(take(std::move(p)))); }

// rules: STN-57
// expect: not-stone: 'x' is used in the same expression as std::move(x)
int operands(std::vector<int> x) { return static_cast<int>(x.size()) + count(std::move(x)); }

// Efter flytten i källordning, men i samma uttryck.
// rules: STN-57
// expect: not-stone: 'x' is used in the same expression as std::move(x)
int after_in_source(std::vector<int> x) { return count(std::move(x)) + static_cast<int>(x.size()); }

// rules: STN-57
// expect: not-stone: 'x' is used in the same expression as std::move(x)
bool in_condition(std::vector<int> x) {
  if (count(std::move(x)) > static_cast<int>(x.size())) return true;
  return false;
}

// Ett annat fält i samma lokala variabel: q.n överlappar inte q.a (STN-100).
// Ingen körning: Pair saknar kopierings- och flyttkonstruktor (se
// sibling_in_same_expression i 149_move_reset.cpp för samma form).
// rules: STN-57, STN-100
// expect: stone: assuming
int other_field_of_local(Pair q) { return q.n + count(std::move(q.a)); }

// Genom en lokal referens till roten.
// rules: STN-57
// expect: not-stone: 'r' is used in the same expression as std::move(x)
int through_reference(std::vector<int> x) {
  const std::vector<int>& r = x;
  return static_cast<int>(r.size()) + count(std::move(x));
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"take", [] { return take(std::make_unique<int>(2)) == 2 && take(nullptr) == -1 ? 0 : 1; }},
    {"count", [] { return count({1, 2}) == 2 ? 0 : 1; }},
    {"id", [] { return id(3) == 3 ? 0 : 1; }},
    {"add", [] { return add(1, 2) == 3 ? 0 : 1; }},
    {"Pair::operator=", [] { Pair p; p.a = {1, 2}; p.n = 1; Pair q; q = std::move(p); return q.n == 3 ? 0 : 1; }},
    {"separate_statements", [] { return separate_statements(std::make_unique<int>(4)) == 4 ? 0 : 1; }},
    {"only_the_move", [] { return only_the_move(std::make_unique<int>(4)) == 5 ? 0 : 1; }},
    {"other_variable", [] { return other_variable({1}, {1, 2}) == 3 ? 0 : 1; }})
#endif
