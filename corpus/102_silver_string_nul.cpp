// pork-args: --silver
//
// En strängliteral når std::string genom en const char*-parameter
// (konstruktorn, ==, +=), så den slutar vid första NUL (SLV-35):
// std::string("a\0b") har storlek 1, och s == "ab\0" jämför med "ab".

#include <string>

// rules: SLV-35
// expect: stone: not silver: division by zero
int bad_size_after_nul() {
  std::string s = "a\0b";
  return 100 / (static_cast<int>(s.size()) - 1);
}

// rules: SLV-35
// expect: silver: assuming
int ok_size_after_nul() {
  std::string s = "a\0b";
  return 100 / static_cast<int>(s.size());
}

// rules: SLV-35
// expect: stone: not silver: division by zero
int bad_compare_nul() {
  std::string s = "ab";
  if (s == "ab\0") return 100 / (static_cast<int>(s.size()) - 2);
  return 1;
}

// rules: SLV-35
// expect: stone: not silver: division by zero
int bad_only_nul() {
  std::string s = "\0";
  return 100 / static_cast<int>(s.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"ok_size_after_nul", [] { return ok_size_after_nul() == 100 ? 0 : 1; }})
#endif
