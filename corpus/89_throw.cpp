// STN-83: throw av en <stdexcept>-typ med en strängliteral avslutar
// funktionen.
// Silver (SLV-43): kastet ska inte kunna nås.
#include <stdexcept>
#include <string>
#include <vector>

// rules: STN-68
// expect: Box::~Box: stone
struct Box { int v; ~Box() {} };
int counter = 0;
// rules: STN-68
// expect: Noisy::~Noisy: not-stone: uses variable 'counter' with static storage
struct Noisy { int v = 0; ~Noisy() { counter = 1; } };

// rules: STN-83
// expect: stone: assuming throws std::length_error
int checked(int x) {
  if (x < 0) throw std::length_error("negative");
  return x;
}

// Destruktorn för en lokal körs också när undantaget lämnar funktionen.
// rules: STN-83, STN-68
// expect: stone: assuming throws std::length_error
int with_local(int x) {
  Box b{x};
  if (x < 0) throw std::length_error("negative");
  return b.v;
}

// rules: STN-83
// expect: stone: assuming throws std::runtime_error
int always(int x) {
  Box b{x};
  throw std::runtime_error("always");
}

// Antagandet följer med till anroparen.
// rules: STN-83
// expect: stone: assuming throws std::length_error
int caller(int x) { return checked(x) + 1; }

// rules: STN-83
// expect: stone: assuming
void grow(std::vector<int>& v, int x) {
  v.push_back(x);
  if (x < 0) throw std::invalid_argument("negative");
  v.push_back(x);
}

// En destruktor som inte är Stone på en väg som bara kastar (G1).
// rules: STN-83, STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int bad_noisy(int x) {
  Noisy n{};
  throw std::runtime_error("x");
}

// rules: STN-83
// expect: not-stone: unsupported expression: CXXThrowExpr
int bad_in_expression(int x) { return x < 0 ? throw std::length_error("negative") : x; }

// rules: STN-83
// expect: not-stone: throws something other than a <stdexcept> type with a string literal
int bad_int(int x) {
  if (x < 0) throw 1;
  return x;
}

// rules: STN-83
// expect: not-stone: the argument of std::length_error must be a string literal
int bad_message(int x, const std::string& why) {
  if (x < 0) throw std::length_error(why);
  return x;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"checked", [] {
       if (checked(3) != 3) return 1;
       try { checked(-1); } catch (const std::length_error&) { return 0; }
       return 1; }},
    {"with_local", [] { try { with_local(-1); } catch (const std::length_error&) { return 0; } return 1; }},
    {"always", [] { try { always(1); } catch (const std::runtime_error&) { return 0; } return 1; }},
    {"caller", [] { try { caller(-1); } catch (const std::length_error&) { return 0; } return 1; }},
    {"grow", [] {
       std::vector<int> v;
       try { grow(v, -1); } catch (const std::invalid_argument&) { return v.size() == 1 ? 0 : 1; }
       return 1; }})
#endif
