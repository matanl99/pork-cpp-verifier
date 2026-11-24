// pork-args: --silver --vector-bound 8
//
// std::string (std::basic_string<char>) som betrodd värdetyp (trust-filen:
// `type std::basic_string`). Silver: samma modell som std::vector,
// { size, data[N] } med N = --vector-bound, och antagandet std::string size <= N.
// Literaler blir strängar med de tecknen, ==/!= jämför längd och tecken,
// += och + kontrollerar att resultatet ryms.

#include <string>
#include <pork/annotations.hpp>

struct Named { std::string name; int id; };

// rules: STN-4, SLV-35
// expect: silver: assuming
unsigned long length_of(const std::string& s) { return s.size() + s.length(); }

// En tom const sträng: s[0] är s[s.size()], alltså '\0' (definierat).
// rules: SLV-35, SLV-13
// expect: silver: assuming
char first(const std::string& s) { return s[0]; }

// rules: SLV-35, SLV-13
// expect: silver: assuming
char first_checked(const std::string& s) { PORK_PRE(!s.empty()); return s[0]; }

// rules: STN-37, SLV-35
// expect: silver: assuming
void capitalise(std::string& s) { PORK_PRE(s.size() > 0); s[0] = 'X'; }

// rules: STN-29, SLV-35
// expect: silver: assuming
bool is_idle(const Named& n) { return n.name == "Idle"; }

// rules: SLV-35
// expect: silver: assuming
void rename(Named& n) { n.name = "Paused"; }

// rules: SLV-35
// expect: silver: assuming
std::string greeting() { std::string s = "hej"; s += ' '; s += "du"; return s; }

// Modellen ger rätt längd: "hej du".
// rules: SLV-35, SLV-18
// expect: silver: assuming
int greeting_length() { PORK_POST(int r, r == 6); return int(greeting().size()); }

// rules: SLV-35, SLV-18
// expect: stone: not silver: postcondition of 'wrong_length'
int wrong_length() { PORK_POST(int r, r == 5); return int(greeting().size()); }

// Jämförelsen ser tecknen.
// rules: SLV-35, SLV-18
// expect: silver: assuming
bool differs() { PORK_POST(bool r, r); std::string a = "abc"; std::string b = "abd"; return a != b; }

// rules: SLV-35
// expect: stone: not silver: std::string may grow beyond 8 elements
std::string join(const std::string& a, const std::string& b) { return a + b; }

// rules: SLV-35, SLV-17
// expect: silver: assuming
std::string join_checked(const std::string& a, const std::string& b) { PORK_PRE(a.size() + b.size() <= 8); return a + b; }

// rules: SLV-35
// expect: stone: not silver: std::string may grow beyond 8 elements
bool too_long() { std::string s = "nine char"; return s.empty(); }
