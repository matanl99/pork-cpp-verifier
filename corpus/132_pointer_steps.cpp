// Pekarsteg som egen sats (STN-39): `++p`, `p--`, `p += n` och `p -= n` är
// pekararitmetik precis som `p + 1`. En pekare till ett enda objekt som stegas
// fram och avrefereras läser utanför objektet. Heltal stegas som vanligt.

// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: ++
int pre_increment(const int& n) {
  const int* p = &n;
  ++p;
  return *p;
}

// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: ++
int post_increment(const int& n) {
  const int* p = &n;
  p++;
  return *p;
}

// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: --
int decrement(const int& n) {
  const int* p = &n;
  --p;
  return *p;
}

// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: +=
int add_assign(const int& n) {
  const int* p = &n;
  p += 3;
  return *p;
}

// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: -=
int sub_assign(const int& n) {
  const int* p = &n;
  p -= 1;
  return *p;
}

// I en for-loop och i en kommasats.
// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: ++
int loop_step(const int& n) {
  int s = 0;
  for (const int* p = &n; p != nullptr; ++p) {
    s = *p;
    break;
  }
  return s;
}

// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: ++
int comma_step(const int& n) {
  const int* p = &n;
  int k = 0;
  (++p, ++k);
  return k;
}

// rules: STN-39
// expect: stone
int integer_steps(int k) {
  ++k;
  k--;
  k += 2;
  k -= 1;
  return k;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"pre_increment", [] { int k = 1; return pre_increment(k) == 0 ? 0 : 0; }},
    {"post_increment", [] { int k = 1; return post_increment(k) == 0 ? 0 : 0; }},
    {"decrement", [] { int k = 1; return decrement(k) == 0 ? 0 : 0; }},
    {"add_assign", [] { int k = 1; return add_assign(k) == 0 ? 0 : 0; }},
    {"sub_assign", [] { int k = 1; return sub_assign(k) == 0 ? 0 : 0; }},
    {"loop_step", [] { int k = 1; return loop_step(k) == 1 ? 0 : 1; }},
    {"comma_step", [] { int k = 1; return comma_step(k) == 1 ? 0 : 1; }},
    {"integer_steps", [] { return integer_steps(1) == 2 ? 0 : 1; }})
#endif
