// Lån lagrade i ett objekt som en funktion får ändra (STN-62). En struct med
// ett span-fält är ett lånevärde. `out.text = s` är ett anrop till
// span::operator=, inte en inbyggd tilldelning: lagringen ska kontrolleras på
// samma sätt (ett lån av en lokal får inte lagras i en parameter). Hos
// anroparen lånar objektet efter ett anrop utan sammanfattning också det som
// de andra argumenten når, så en ändring av b medan c används avvisas.

#include <span>
#include <string>

struct Cursor {
  std::span<const char> text;
  std::size_t pos;

  // rules: STN-62
  // expect: stone: assuming type std::span
  char peek() const { return text[pos]; }
};

// rules: STN-62
// expect: stone: assuming type std::span
char use(const Cursor& c) { return c.text[c.pos]; }

// rules: STN-62
// expect: stone: assuming type std::allocator, type std::basic_string, type std::span
void point_at(Cursor& out, const std::string& s) {
  out.text = s;
  out.pos = 0;
}

// rules: STN-62
// expect: not-stone: loans: stores a loan of 't[*]', which does not outlive the function (line 35)
void bad_store_local(Cursor& out) {
  std::string t = "abcdefghijklmnopqrstuvwxyz0123456789";
  out.text = t;
  out.pos = 0;
}

// rules: STN-62
// expect: not-stone: loans: may store a loan of 't[*]', which does not outlive the function (line 43)
void bad_point_at_local(Cursor& c) {
  std::string t = "abcdefghijklmnopqrstuvwxyz0123456789";
  point_at(c, t);
}

// rules: STN-62
// expect: not-stone: loans: changes 'b' while 'c' borrows 'b[*]' and is used later (line 51)
char bad_change_after(const std::string& a, std::string& b) {
  Cursor c{a, 0};
  point_at(c, b);
  b += "a much longer string that forces a reallocation of b";
  return use(c);
}

// rules: STN-62
// expect: stone: assuming std::basic_string::operator+=, type std::allocator, type std::basic_string, type std::span
char change_before(const std::string& a, std::string& b) {
  b += "x";
  Cursor c{a, 0};
  point_at(c, b);
  return use(c);
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_store_local", [] { Cursor c{}; bad_store_local(c); return use(c) == 'a' ? 0 : 1; }},
    {"bad_point_at_local", [] { Cursor c{}; bad_point_at_local(c); return c.peek() == 'a' ? 0 : 1; }},
    {"bad_change_after", [] {
       std::string a = "q", b = "a long string that does not fit in the small buffer";
       return bad_change_after(a, b) == 'a' ? 0 : 1;
     }},
    {"change_before", [] { std::string a = "q", b = "r"; return change_before(a, b) == 'r' ? 0 : 1; }})
#endif
