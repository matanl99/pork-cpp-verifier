// pork-args: --silver --vector-bound 4
//
// std::span<const T> i Silver: { size, const T* data }. Från en vektor pekar
// data in i vektorns data[N]. En span-parameter i harnessen pekar på en egen
// array med N element, och size <= N (den kommer alltid från en vektor).

#include <pork/annotations.hpp>

#include <span>
#include <vector>

// rules: SLV-26
// expect: silver: assuming
int count(std::span<const int> s) { int n = 0; for (const int x : s) if (x > 0) n = n + 1; return n; }

// rules: SLV-15
// expect: stone: not silver: precondition of 'std::span::operator[]'
int at_any(std::span<const int> s, unsigned long i) { return s[i]; }

// rules: SLV-15, SLV-17
// expect: silver: assuming
int at_checked(std::span<const int> s, unsigned long i) { PORK_PRE(i < s.size()); return s[i]; }

// rules: SLV-15
// expect: stone: not silver: precondition of 'std::span::front'
int front_any(std::span<const int> s) { return s.front(); }

// rules: SLV-26, SLV-16
// expect: silver: assuming
int loop_index(std::span<const int> s) { int n = 0; for (unsigned long i = 0; i < s.size(); ++i) n = n + (s[i] != 0); return n; }

// Spanen ser vektorns innehåll: eftervillkoret bevisas genom anropet.
// rules: SLV-15, SLV-17
// expect: silver: assuming
int last(std::span<const int> s) { PORK_PRE(!s.empty()); return s.back(); }

// rules: SLV-26, SLV-18
// expect: silver: assuming
int through_span() { PORK_POST(int r, r == 7); std::vector<int> v{}; v.push_back(3); v.push_back(7); return last(v); }

// rules: SLV-17
// expect: stone: not silver: precondition of 'last'
int through_empty() { std::vector<int> v{}; return last(v); }
