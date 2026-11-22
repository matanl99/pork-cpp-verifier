// Forwarding-referenser i std (STN-25): en konstruktor bygger sitt objekt av
// argumentet; andra funktioner kan skriva genom U&& = T& (std::ranges::sort,
// std::invoke) om inte std.txt säger att de bara läser.
#include <functional>
#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// --- godtas -----------------------------------------------------------------

// rules: STN-25
// expect: stone: assuming
int first_of(const std::vector<int>& v) {
  std::span<const int> s(v);
  return s.empty() ? 0 : s[0];
}

// rules: STN-25
// expect: stone: assuming
int pair_first(int a, int b) { return std::pair<int, int>(a, b).first + a; }

// rules: STN-25
// expect: stone: assuming
int optional_copy(int a) {
  std::optional<int> o(a);
  return *o + a;
}

// value_or är ingen konstruktor: U&& kan skrivas (raden i std.txt gäller
// *this, P-d). Avvisas, fast int-konverteringen inte skriver.
// rules: STN-25, STN-21
// expect: not-stone: side effect inside expression
int fallback(const std::optional<int>& o, int d) { return o.value_or(d) + d; }

// rules: STN-25
// expect: stone: assuming
int fallback_statement(const std::optional<int>& o, int d) {
  int r = o.value_or(d);
  return r + d;
}

// rules: STN-25, STN-78
// expect: stone: assuming
void sort_statement(std::vector<int>& v) { std::ranges::sort(v); }

// --- avvisas: anropet skriver genom U&& -----------------------------------------

// Fanns före: R&& räknades som läsning, så sorteringen inuti uttrycket
// syntes inte som en sidoeffekt.
// rules: STN-25, STN-21
// expect: not-stone: side effect inside expression
int sort_in_expression(std::vector<int>& v) { return std::ranges::sort(v) == v.end() ? 1 : 0; }

// rules: STN-25, STN-21, STN-79
// expect: not-stone: side effect inside expression
int invoke_writes(int x) {
  return std::invoke([](int& y) { y = y + 1; return y; }, x) + x;
}

// --- adversariell granskning (I1): användarkod som skriver ----------------------

struct Bump {
  int v;
  // rules: STN-66
  // expect: stone
  Bump(int& a) : v(a) { a = a + 1; }
};
struct Cnt {
  int n;
  int reads;
  // rules: STN-8
  // expect: stone
  operator int() {
    reads = reads + 1;
    return n;
  }
};

// Konstruktorn når Bump(int&): argumentet skrivs, som i Bump(a).v + a.
// rules: STN-25, STN-21
// expect: not-stone: side effect inside expression
int opt_ctor_writes(int& a) { return std::optional<Bump>(a)->v + a; }

// rules: STN-25, STN-21
// expect: not-stone: side effect inside expression
int pair_ctor_writes(int& a) { return std::pair<Bump, int>(a, 1).first.v + a; }

// rules: STN-25, STN-21
// expect: not-stone: side effect inside expression
int value_or_ctor_writes(const std::optional<Bump>& o, int& a) { return o.value_or(a).v + a; }

// rules: STN-25, STN-21
// expect: not-stone: side effect inside expression
int value_or_conversion_writes(const std::optional<int>& o, Cnt& c) { return o.value_or(c) + c.reads; }

// Samma sak utan std: samma dom.
// rules: STN-21
// expect: not-stone: side effect inside expression
int direct_ctor_writes(int& a) { return Bump(a).v + a; }
