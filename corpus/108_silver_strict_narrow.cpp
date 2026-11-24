// pork-args: --silver --silver-strict
//
// Samma fall som 107_silver_narrow_arithmetic.cpp med --silver-strict: att
// konvertera tillbaka till den smalare typen kan tappa värdet (SLV-30).

// rules: SLV-30
// expect: stone: not silver: integer conversion out of range at 108_silver_strict_narrow.cpp:9
char inc_char(char c) {
  c++;
  return c;
}

// rules: SLV-30
// expect: stone: not silver: integer conversion out of range at 108_silver_strict_narrow.cpp:16
char dec_char(char c) {
  --c;
  return c;
}

// rules: SLV-30
// expect: stone: not silver: integer conversion out of range at 108_silver_strict_narrow.cpp:23
short add_short(short a) {
  a += 1;
  return a;
}

// rules: SLV-29, SLV-30
// expect: stone: not silver: integer conversion out of range at 108_silver_strict_narrow.cpp:30
unsigned short sub_ushort(unsigned short u) {
  u -= 1;
  return u;
}

// rules: SLV-30
// expect: silver
short add_short_bounded(short a) {
  if (a < 100) a += 1;
  return a;
}
