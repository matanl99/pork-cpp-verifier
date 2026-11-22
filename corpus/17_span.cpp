// std::span<const T> som parameter: en läsvy över en std::vector.
//
//  - Bara std::span<const T> (dynamisk storlek) som parameter, med T en
//    värdetyp. Inte som lokal variabel, fält eller returvärde: en span pekar in
//    i någon annans minne och får inte överleva anropet.
//  - Vid anropet skapas spanen bara från en std::vector (eller en annan
//    span-parameter). Det räknas som ett const&-argument: vektorn får inte
//    överlappa ett T&-argument i samma anrop.
//  - I kroppen: s[i], s.size(), s.empty(), s.front(), s.back() och range-for.
//    Inga subspan/first/last/data än.

#include <span>
#include <vector>

struct P { int x{0}; };

// rules: STN-41
// expect: stone
int sum(std::span<const int> s) { int n = 0; for (const int x : s) n = n + x; return n; }

// rules: STN-41
// expect: stone
int first_x(std::span<const P> s) { if (s.empty()) return 0; return s.front().x + s[0].x + int(s.size()); }

// rules: STN-41
// expect: stone: assuming
int call_sum(const std::vector<int>& v) { return sum(v); }

// Från en annan span-parameter.
// rules: STN-41
// expect: stone
int pass_on(std::span<const int> s) { return sum(s); }

// rules: STN-41
// expect: stone: assuming
int local_vector() { std::vector<int> v{}; v.push_back(1); return sum(v); }

struct W { std::vector<int> xs; int total{0}; };

// rules: STN-41
// expect: stone: assuming
void add_all(W& w, std::span<const int> s) { for (const int x : s) w.total = w.total + x; }

// Samma vektor som vy och inuti T&-argumentet: aliasing.
// rules: STN-41, STN-62
// expect: not-stone: loans: operands 'w' and 'w.xs[*]' of the call to 'add_all' may be the same object
// loans: rejected: may be the same object
void alias(W& w) { add_all(w, w.xs); }

// rules: STN-41
// expect: stone: assuming
void no_alias(W& w, const std::vector<int>& other) { add_all(w, other); }

// rules: STN-41, STN-12
// expect: not-stone: parameter 's' has type std::span<int>
int mutable_span(std::span<int> s) { return int(s.size()); }

// En lokal span är ett lånevärde; lånemodellen följer lånet av v.
// rules: STN-62
// expect: stone: assuming
// loans: ok
int local_span(const std::vector<int>& v) { std::span<const int> s = v; return int(s.size()); }

// rules: STN-62
// expect: stone
// loans: ok
int sub(std::span<const int> s) { return sum(s.first(1)); }

// rules: STN-41
// expect: not-stone: parameter 's' has type std::span<const int, 2>
int fixed(std::span<const int, 2> s) { return s[0]; }
