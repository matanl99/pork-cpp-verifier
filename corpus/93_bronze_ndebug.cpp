// Med NDEBUG blir assert ((void)0), men den står kvar på samma plats i
// kontraktsinledningen: ett krav efter den är på tillåten plats (BRZ-1).
#define NDEBUG
#include <cassert>
#include <pork/annotations.hpp>

// rules: BRZ-1
// expect: bronze
int after_assert(int x) {
  assert(x >= 0);
  PORK_DEPENDS_RESULT(x);
  return x + 1;
}
// rules: BRZ-1
// expect: bronze
int pre_after_assert(int x) {
  assert(x >= 0);
  PORK_PRE(x < 100);
  PORK_DEPENDS_RESULT(x);
  return x + 1;
}
