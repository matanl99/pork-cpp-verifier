// pork-args: --silver
//
// ++, -- och x op= y på typer smalare än int räknas i int och konverteras
// tillbaka ([expr.pre.incr], [expr.ass]). Additionen kan inte svämma över, och
// konverteringen är definierad (modulo sedan C++20): inget fynd utan
// --silver-strict (se 108_silver_strict_narrow.cpp). Ett smalare flyttal
// (float += double) kontrolleras alltid (SLV-9).

// rules: SLV-5, SLV-30
// expect: silver
char inc_char(char c) {
  c++;
  return c;
}

// rules: SLV-5, SLV-30
// expect: silver
char dec_char(char c) {
  --c;
  return c;
}

// rules: SLV-5, SLV-30
// expect: silver
short add_short(short a) {
  a += 1;
  return a;
}

// rules: SLV-29, SLV-30
// expect: silver
unsigned short sub_ushort(unsigned short u) {
  u -= 1;
  return u;
}

// rules: SLV-5
// expect: stone: not silver: arithmetic overflow on signed +
int add_int(int a) {
  a += 1;
  return a;
}

// rules: SLV-9
// expect: stone: not silver: floating-point conversion overflow at 107_silver_narrow_arithmetic.cpp:47
float add_double_to_float(float f, double d) {
  f += d;
  return f;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"inc_char", [] { return inc_char('a') == 'b' ? 0 : 1; }},
    {"dec_char", [] { return dec_char('b') == 'a' ? 0 : 1; }},
    {"add_short", [] { return add_short(1) == 2 ? 0 : 1; }},
    {"sub_ushort", [] { return sub_ushort(1) == 0 ? 0 : 1; }})
#endif
