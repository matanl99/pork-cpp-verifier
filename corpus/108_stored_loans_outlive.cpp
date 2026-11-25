// Lån lagrade utanför funktionen (STN-92). Ett lån som lagras i en parameter
// eller i *this lever efter funktionen: en senare ändring av det lånade i
// funktionen avvisas, också om parametern inte används igen (anroparen ser
// lånet efteråt). Ett lån av ett objekts egen lagring lagras aldrig i samma
// objekt: en kopia av objektet lånar originalet.
#include <cstddef>
#include <span>
#include <string>

struct Cursor {
  std::span<const char> text;
  std::size_t pos;
};

// rules: STN-92
// expect: stone: assuming
char use(const Cursor& c) { return c.text[c.pos]; }

// rules: STN-92
// expect: stone: assuming
void grow(std::string& s) { s += "a much longer string that forces a reallocation of s"; }

// --- godtas -----------------------------------------------------------------

// rules: STN-92
// expect: stone: assuming
void store(Cursor& p, const std::string& s) { p.text = s; }

// Ändringen före lagringen.
// rules: STN-92
// expect: stone: assuming
char change_then_store(Cursor& p, std::string& s) {
  grow(s);
  p.text = s;
  return use(p);
}

struct View {
  std::span<const char> v;
  // rules: STN-92
  // expect: stone: assuming
  void set(const std::string& s) { v = s; }
  // rules: STN-92
  // expect: stone: assuming
  char get() const { return v[0]; }
};

struct FromParameter {
  std::string t;
  std::span<const char> v;
  // Ett lån av parametern, inte av det egna fältet.
  // rules: STN-92, STN-66
  // expect: stone: assuming
  explicit FromParameter(const std::string& s) : t(s), v(s) {}
};

// --- underkänns -------------------------------------------------------------

// rules: STN-92
// expect: not-stone: loans: changes 's' while 'p.text' holds a loan of 's[*]', which outlives the function
char store_then_change(Cursor& p, std::string& s) {
  p.text = s;
  s += "a much longer string that forces a reallocation of s";
  return use(p);
}

// Parametern används inte igen, men anroparen läser den efteråt.
// rules: STN-92
// expect: not-stone: loans: changes 's' while 'p.text' holds a loan of 's[*]', which outlives the function
void store_then_grow(Cursor& p, std::string& s) {
  p.text = s;
  grow(s);
}

struct Holder {
  std::span<const char> v;
  // rules: STN-92
  // expect: not-stone: loans: changes 's' while '*this.v' holds a loan of 's[*]', which outlives the function
  void hold_and_grow(std::string& s) {
    v = s;
    grow(s);
  }
};

struct Self {
  std::string t;
  std::span<const char> v;
  // rules: STN-92
  // expect: not-stone: loans: stores a loan of '*this.t[*]' in '*this.v', which may be the same object
  void bind() { v = t; }
};

// rules: STN-92
// expect: not-stone: loans: stores a loan of 'x.t[*]' in 'x.v', which may be the same object
void bind(Self& x) { x.v = x.t; }

struct SelfInit {
  std::string t;
  std::span<const char> v{t};
};

// Standardinitieraren lånar ett annat fält (STN-66).
// rules: STN-92, STN-66
// expect: not-stone: initializer of field 'v' uses *this
char copy_of_self_init(const std::string& s) {
  SelfInit a{s};
  SelfInit b = a;
  return b.v[0];
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"use", [] { std::string t = "abc"; Cursor c{t, 1}; return use(c) == 'b' ? 0 : 1; }},
    {"grow", [] { std::string s = "a"; grow(s); return s.size() > 1 ? 0 : 1; }},
    {"store", [] { std::string t = "abc"; Cursor c{}; store(c, t); return c.text.size() == 3 ? 0 : 1; }},
    {"change_then_store", [] { Cursor c{}; std::string s = "abc"; return change_then_store(c, s) == 'a' ? 0 : 1; }},
    {"View::set", [] { std::string t = "abc"; View w{}; w.set(t); return w.v.size() == 3 ? 0 : 1; }},
    {"View::get", [] { std::string t = "abc"; View w{t}; return w.get() == 'a' ? 0 : 1; }},
    {"FromParameter::FromParameter", [] { std::string t = "abc"; FromParameter f(t); return f.v[0] == 'a' ? 0 : 1; }})
#endif
