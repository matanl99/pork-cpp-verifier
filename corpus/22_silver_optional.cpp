// pork-args: --silver
//
// std::optional<T> i Silver: { has, v }. Operationerna modelleras direkt och
// redovisas som antaganden. *o och value() kräver has_value().

#include <pork/annotations.hpp>

#include <optional>

struct Vec2 { float x; float y; };

// rules: SLV-22
// expect: silver
bool small(Vec2 v) { return v.x > -1e6F && v.x < 1e6F && v.y > -1e6F && v.y < 1e6F; }

// rules: SLV-23
// expect: silver: assuming
int or_zero(std::optional<int> o) { return o.value_or(0); }

// rules: SLV-14
// expect: stone: not silver: precondition of 'std::optional::operator*'
int deref_any(std::optional<int> o) { return *o; }

// rules: SLV-14
// expect: stone: not silver: precondition of 'std::optional::value'
int value_any(const std::optional<int>& o) { return o.value(); }

// rules: SLV-14, SLV-23
// expect: silver: assuming
int deref_checked(std::optional<int> o) { if (o) return *o; return 0; }

// rules: SLV-14, SLV-23
// expect: silver: assuming
int has_checked(const std::optional<int>& o) { return o.has_value() ? *o : -1; }

// rules: SLV-23
// expect: silver: assuming
std::optional<int> positive(int a) { if (a > 0) return a; return std::nullopt; }

// rules: SLV-23, SLV-17
// expect: silver: assuming
int roundtrip(int a) { PORK_PRE(a > 0 && a < 100); std::optional<int> o = positive(a); return *o + 1; }

// rules: SLV-14
// expect: stone: not silver: precondition of 'std::optional::operator*'
int roundtrip_any(int a) { std::optional<int> o = positive(a); return *o; }

// rules: SLV-23
// expect: silver: assuming
void set(std::optional<int>& o, int v) { o = v; }

// rules: SLV-23
// expect: silver: assuming
void clear(std::optional<int>& o) { o.reset(); }

// rules: SLV-23
// expect: silver: assuming
void clear_nullopt(std::optional<int>& o) { o = std::nullopt; }

// rules: SLV-23
// expect: silver: assuming
int empty_then_set(int v) { std::optional<int> o; o = v; return *o; }

// rules: SLV-23
// expect: silver: assuming
long widened(int v) { std::optional<long> o = v; return o.value_or(0L); }

// rules: SLV-23, SLV-17
// expect: silver: assuming
float field_of(const std::optional<Vec2>& p) { PORK_PRE(!p || small(*p)); if (!p) return 0.0F; return (*p).x + (*p).y; }

// rules: SLV-9
// expect: stone: not silver: arithmetic overflow on floating-point addition
float field_of_any(const std::optional<Vec2>& p) { if (!p) return 0.0F; return (*p).x + (*p).y; }
