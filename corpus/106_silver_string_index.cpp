// pork-args: --silver
//
// s[s.size()] på en const std::string är definierat och ger '\0'
// ([string.access], SLV-35). Bara const-överlagringen godtar pos == size():
// den ger const char&, så ingen skrivning når dit.

#include <string>

// rules: SLV-35
// expect: silver: assuming
int ok_terminator(const std::string& s) { return 100 / (s[s.size()] + 1); }

// rules: SLV-35
// expect: stone: not silver: precondition of 'std::string::operator[]': index <= size()
char bad_past_end(const std::string& s) { return s[s.size() + 1]; }

// rules: SLV-35
// expect: stone: not silver: precondition of 'std::string::operator[]': index < size()
void bad_write_at_size(std::string& s) { s[s.size()] = 'x'; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"ok_terminator", [] { return ok_terminator("ab") == 100 ? 0 : 1; }})
#endif
