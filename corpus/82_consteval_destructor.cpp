// Adversariell granskning (F4): ett consteval-anrop vars resultat
// är en tom klass räknades som en konstant (STN-80). Binds resultatet till en
// referens materialiseras en temporär vid körning, och dess destruktor körs
// vid körning. Undantaget gäller nu bara en trivial destruktor (P-b i
// docs/stone_model.md); annars granskas destruktorn som vanligt.
#include <type_traits>

int counter = 0;

struct Leak {
  // rules: STN-23
  // expect: not-stone: static storage
  constexpr ~Leak() {
    if (!std::is_constant_evaluated()) counter = counter + 1;
  }
};

// rules: STN-80
// expect: stone
consteval Leak mk() { return {}; }

// rules: STN-23
// expect: stone
void take(const Leak&) {}

// rules: STN-80, STN-68, STN-82
// expect: not-stone: calls non-stone 'Leak::~Leak'
int bad_consteval_ref_arg() {
  take(mk());
  return 0;
}

// rules: STN-80, STN-68, STN-82
// expect: not-stone: calls non-stone 'Leak::~Leak'
int bad_consteval_bound() {
  const Leak& l = mk();
  (void)l;
  return 0;
}

struct Tag {};
// rules: STN-80
// expect: stone
consteval Tag tag() { return {}; }
// rules: STN-23
// expect: stone
void use(const Tag&) {}

// En trivial destruktor: ingen kod körs.
// rules: STN-80
// expect: stone
int tag_arg() {
  use(tag());
  return 0;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
// G4: destruktorn skriver `counter`; en Stone-funktion får inte ändra den.
PORK_RUN_CASES(
    {"bad_consteval_ref_arg", [] { counter = 0; bad_consteval_ref_arg(); return counter; }},
    {"bad_consteval_bound", [] { counter = 0; bad_consteval_bound(); return counter; }},
    {"tag_arg", [] { counter = 0; tag_arg(); return counter; }})
#endif
