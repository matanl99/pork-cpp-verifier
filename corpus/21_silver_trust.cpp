// pork-args: --silver
//
// Betrodda funktioner i Silver: anropet ersätts med ett kontrakt i C
// (contracts/std.c för standardbiblioteket, --contracts fil.c för egna).
// Kontraktets namn: kvalificerat namn med :: -> __, följt av parametertyperna
// utan const/&: std::hypot(float, float) -> std__hypot__float__float.
// Kontraktet kan ha förvillkor (std::clamp: !(hi < lo)).

#include <pork/annotations.hpp>

#include <algorithm>
#include <cmath>

float external(float x);  // betrodd i 21_silver_trust.trust, men inget kontrakt

// rules: SLV-21
// expect: silver: assuming std::hypot
float length(float x, float y) { PORK_PRE(x > -1e6F && x < 1e6F && y > -1e6F && y < 1e6F); return std::hypot(x, y); }

// rules: SLV-21
// expect: stone: not silver: std::hypot overflows
float length_any(float x, float y) { return std::hypot(x, y); }

// rules: SLV-21
// expect: silver: assuming std::fabs
float gap(float a, float b) { PORK_PRE(a > -1e6F && a < 1e6F && b > -1e6F && b < 1e6F); return std::fabs(a - b); }

// rules: SLV-21, SLV-17
// expect: stone: not silver: precondition of 'std::clamp'
float clamp_any(float v, float lo, float hi) { return std::clamp(v, lo, hi); }

// rules: SLV-21
// expect: silver: assuming std::clamp
float clamp_ok(float v, float hi) { PORK_PRE(hi >= 0.0F); return std::clamp(v, 0.0F, hi); }

// rules: SLV-21
// expect: stone: not silver: no contract for 'external'
float use_external(float x) { return external(x); }

// rugby::velocity_of i liten form.
// rules: SLV-21, SLV-9
// expect: stone: not silver: arithmetic overflow on floating-point division
float speed_towards(float dx, float dy, float top) {
  PORK_PRE(dx > -1e3F && dx < 1e3F && dy > -1e3F && dy < 1e3F && top >= 0.0F && top <= 20.0F);
  const float remaining = std::hypot(dx, dy);
  if (remaining <= 0.0F) return 0.0F;
  return top / remaining;
}
