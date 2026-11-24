// Meddelanden som pekar på rätt alias.
//
// s.m(s.own) med en klass som också har ett referensfält: det verkliga aliaset
// är s och s.own (STN-62/STN-53), inte referensfältet. En skalär har ingen
// ägd lagring: std::max(a, b) ger a eller b, en referens till den själv, som
// `const int& r = a;` (ett alias, inte ett lån av a[*]).

#include <algorithm>
#include <vector>

struct Ctx {
  std::vector<int> v;
};

struct Svc {
  Ctx own;
  Ctx& ref;

  // rules: STN-53
  // expect: stone: assuming std::vector::push_back, type std::allocator, type std::vector
  void add(Ctx& other) { other.v.push_back(1); }
};

// rules: STN-62, STN-53
// expect: not-stone: operands 's' and 's.own' of the call to 'Svc::add' may be the same object and one is changed
void bad_self_alias(Svc& s) { s.add(s.own); }

// rules: STN-62
// expect: stone: assuming std::max
int larger_then_clear(int a, int b) {
  const int& r = std::max(a, b);
  b = 0;
  return r;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"larger_then_clear", [] { return larger_then_clear(1, 2) == 0 && larger_then_clear(3, 2) == 3 ? 0 : 1; }})
#endif
