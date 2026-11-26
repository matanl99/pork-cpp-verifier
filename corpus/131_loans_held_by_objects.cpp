// Ett lån som en funktion ger ut genom ett lånefält i ett objekt utanför sig
// (`*this`, en referensparameter) lånar det objektet håller hos anroparen
// (STN-93), inte något okänt utanför. Annars märks det inte när ursprunget
// är en lokal variabel som ändras eller dör.
#include <cstddef>
#include <span>
#include <string>

struct Cursor {
  std::span<const char> text;
  std::size_t pos;
  // rules: STN-93
  // expect: stone: assuming type std::span
  const char& cur() const { return text[pos]; }
  // rules: STN-93
  // expect: stone: assuming type std::span
  std::span<const char> rest() const { return text.subspan(pos); }
};

// rules: STN-93
// expect: stone: assuming type std::span
const char& at(const Cursor& c) { return c.text[c.pos]; }

struct Holder {
  Cursor c;
  // rules: STN-93
  // expect: stone: assuming type std::span
  const char& cur() const { return c.text[c.pos]; }
};

// --- godtas -------------------------------------------------------------------

// rules: STN-93
// expect: stone: assuming
char read_through_method(int n) {
  std::string s = "abc";
  Cursor c{s, 0};
  const char& r = c.cur();
  char out = r;
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return out;
}

// En ändring av något annat än ursprunget.
// rules: STN-93
// expect: stone: assuming
char other_changed(const std::string& s, std::string& t) {
  Cursor c{s, 0};
  const char& r = c.cur();
  t += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return r;
}

// --- avvisas ------------------------------------------------------------------

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'r' borrows 's[*]'
char changed_through_method(int n) {
  std::string s = "abc";
  Cursor c{s, 0};
  const char& r = c.cur();
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return r;
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'r' borrows 's[*]'
char changed_through_parameter(int n) {
  std::string s = "abc";
  Cursor c{s, 0};
  const char& r = at(c);
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return r;
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'v' borrows 's[*]'
char changed_through_view(int n) {
  std::string s = "abcd";
  Cursor c{s, 1};
  std::span<const char> v = c.rest();
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return v[0];
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'r' borrows 's[*]'
char changed_through_field(int n) {
  std::string s = "abc";
  Holder h{{s, 0}};
  const char& r = h.cur();
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return r;
}

// Ursprunget dör medan en pekare till elementet lever.
// rules: STN-93
// expect: not-stone: loans:
char origin_dies() {
  const char* p = nullptr;
  {
    std::string s = "abcxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    Cursor c{s, 0};
    const char& r = c.cur();
    p = &r;
  }
  return *p;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"Cursor::cur", [] { std::string s = "ab"; Cursor c{s, 1}; return c.cur() == 'b' ? 0 : 1; }},
    {"Cursor::rest", [] { std::string s = "ab"; Cursor c{s, 1}; return c.rest().size() == 1 ? 0 : 1; }},
    {"at", [] { std::string s = "ab"; Cursor c{s, 0}; return at(c) == 'a' ? 0 : 1; }},
    {"Holder::cur", [] { std::string s = "ab"; Holder h{{s, 0}}; return h.cur() == 'a' ? 0 : 1; }},
    {"read_through_method", [] { return read_through_method(5) == 'a' ? 0 : 1; }},
    {"other_changed", [] { std::string s = "a"; std::string t; return other_changed(s, t) == 'a' ? 0 : 1; }},
    {"changed_through_method", [] { return changed_through_method(5) == 'a' ? 0 : 1; }},
    {"changed_through_parameter", [] { return changed_through_parameter(5) == 'a' ? 0 : 1; }},
    {"changed_through_view", [] { return changed_through_view(5) == 'b' ? 0 : 1; }},
    {"changed_through_field", [] { return changed_through_field(5) == 'a' ? 0 : 1; }},
    {"origin_dies", [] { return origin_dies() == 'a' ? 0 : 1; }})
#endif
