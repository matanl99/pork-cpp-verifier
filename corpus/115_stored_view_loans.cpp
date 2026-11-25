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

struct Outer {
  Cursor c;
  int n;
};

// rules: STN-62
// expect: stone: assuming type std::span
void Cursor_reset(Cursor& c, std::span<const char> t) {
  c.text = t;
  c.pos = 0;
}

// Hela objektet via Cursor::operator=, från en temporär och från en lokal.
// rules: STN-62
// expect: not-stone: loans: may store a loan of 't[*]', which does not outlive the function (line 81)
void bad_assign_whole(Cursor& out) {
  std::string t = "abcdefghijklmnopqrstuvwxyz0123456789";
  out = Cursor{t, 0};
}

// rules: STN-62
// expect: not-stone: loans: may store a loan of 't[*]', which does not outlive the function (line 89)
void bad_assign_local(Cursor& out) {
  std::string t = "abcdefghijklmnopqrstuvwxyz0123456789";
  Cursor l{t, 0};
  out = l;
}

// Genom en funktion som lagrar, i ett fält av ett annat objekt.
// rules: STN-62
// expect: not-stone: loans: may store a loan of 't[*]', which does not outlive the function (line 97)
void bad_reset_field(Outer& o) {
  std::string t = "abcdefghijklmnopqrstuvwxyz0123456789";
  Cursor_reset(o.c, t);
}

// Cursor_reset lagrar ett lån av t i c: det står i dess sammanfattning
// (STN-93), så anroparen vet att inget skrivs genom lånen i out.
// rules: STN-62, STN-93
// expect: stone: assuming
void reset_from_param(Cursor& out, const std::string& s) { Cursor_reset(out, s); }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_store_local", [] { Cursor c{}; bad_store_local(c); return use(c) == 'a' ? 0 : 1; }},
    {"bad_point_at_local", [] { Cursor c{}; bad_point_at_local(c); return c.peek() == 'a' ? 0 : 1; }},
    {"bad_change_after", [] {
       std::string a = "q", b = "a long string that does not fit in the small buffer";
       return bad_change_after(a, b) == 'a' ? 0 : 1;
     }},
    {"bad_assign_whole", [] { Cursor c{}; bad_assign_whole(c); return use(c) == 'a' ? 0 : 1; }},
    {"bad_assign_local", [] { Cursor c{}; bad_assign_local(c); return use(c) == 'a' ? 0 : 1; }},
    {"bad_reset_field", [] { Outer o{}; bad_reset_field(o); return use(o.c) == 'a' ? 0 : 1; }},
    {"change_before", [] { std::string a = "q", b = "r"; return change_before(a, b) == 'r' ? 0 : 1; }},
    {"reset_from_param", [] { std::string s = "abc"; Cursor c{}; reset_from_param(c, s); return use(c) == 'a' ? 0 : 1; }})
#endif
