// Lån lagrade i objekt (STN-93). Ett lån som lagras i ett fält i en lokal
// variabel (`c.text = t`) blir ett lån som variabeln håller: ursprunget växer
// och krymper aldrig (ett ursprung per objekt). En egen funktion som lagrar
// ett lån i en parameter eller i *this har det i sin sammanfattning ("lagrar
// ett lån av det argument j når i argument k"), så anroparen följer lånet. Ett
// lån av en temporär dinglar när det lagras i en variabel som används senare.
#include <cstddef>
#include <span>
#include <string>

struct Cursor {
  std::span<const char> text;
  std::size_t pos;
};

// rules: STN-93
// expect: stone: assuming
char use(const Cursor& c) { return c.text[c.pos]; }

// rules: STN-93
// expect: stone: assuming
void store(Cursor& p, const std::string& s) { p.text = s; }

// rules: STN-93
// expect: stone: assuming
void forward(Cursor& p, const std::string& s) { store(p, s); }

struct View {
  std::span<const char> v;
  // rules: STN-93
  // expect: stone: assuming
  void set(const std::string& s) { v = s; }
  // rules: STN-93
  // expect: stone: assuming
  char get() const { return v[0]; }
};

struct Two {
  Cursor a;
  Cursor b;
};

// --- godtas -----------------------------------------------------------------

// rules: STN-93
// expect: stone: assuming
char field_of_local(const std::string& s) {
  Cursor c{};
  c.text = s;
  return use(c);
}

// Det andra ursprunget ändras inte.
// rules: STN-93
// expect: stone: assuming
char other_origin_unchanged(const std::string& a, const std::string& b) {
  Cursor c{b, 0};
  c.text = a;
  return use(c);
}

// rules: STN-93
// expect: stone: assuming
char stored_by_call(const std::string& s) {
  Cursor c{};
  store(c, s);
  return use(c);
}

// rules: STN-93
// expect: stone: assuming
char stored_by_method(const std::string& s) {
  View w{};
  w.set(s);
  return w.get();
}

// rules: STN-93
// expect: stone: assuming
char nested_field(const std::string& s, std::string& t) {
  Two w{};
  w.a.text = s;
  t += "x";
  return use(w.a);
}

// Den lokala variabeln används inte efter temporärens slut.
// rules: STN-93
// expect: stone: assuming
char temporary_unused() {
  Cursor c{};
  store(c, std::string("abcdefghijklmnopqrstuvwxyz0123456789"));
  return 'x';
}

// --- underkänns -------------------------------------------------------------

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'c' borrows 's[*]' and is used later
char field_then_change(std::string& s) {
  Cursor c{};
  c.text = s;
  s += "a much longer string that forces a reallocation of s";
  return use(c);
}

// Ursprunget växer: c lånar också b efter tilldelningen (för försiktigt men sunt).
// rules: STN-93
// expect: not-stone: loans: changes 'b' while 'c' borrows 'b[*]' and is used later
char origin_grows(const std::string& a, std::string& b) {
  Cursor c{b, 0};
  c.text = a;
  b += "x";
  return use(c);
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'c' borrows 's[*]' and is used later
char call_then_change(std::string& s) {
  Cursor c{};
  store(c, s);
  s += "a much longer string that forces a reallocation of s";
  return use(c);
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'w' borrows 's[*]' and is used later
char method_then_change(std::string& s) {
  View w{};
  w.set(s);
  s += "a much longer string that forces a reallocation of s";
  return w.get();
}

// Genom två nivåer av sammanfattningar, i en parameter.
// rules: STN-93, STN-92
// expect: not-stone: loans: changes 's' while 'p.text' holds a loan of 's[*]', which outlives the function
void forwarded_then_change(Cursor& p, std::string& s) {
  forward(p, s);
  s += "a much longer string that forces a reallocation of s";
}

// rules: STN-93
// expect: not-stone: loans: may store a loan of 't[*]', which does not outlive the function
void local_into_parameter(Cursor& out) {
  std::string t = "abcdefghijklmnopqrstuvwxyz0123456789";
  store(out, t);
}

// rules: STN-93
// expect: not-stone: loans: 'c' borrows a temporary that is destroyed at the end of the full-expression
char temporary_through_call() {
  Cursor c{};
  store(c, std::string("abcdefghijklmnopqrstuvwxyz0123456789"));
  return use(c);
}

// rules: STN-93
// expect: not-stone: loans: 'c' borrows a temporary that is destroyed at the end of the full-expression
char temporary_into_field() {
  Cursor c{};
  c.text = std::string("abcdefghijklmnopqrstuvwxyz0123456789");
  return use(c);
}

// rules: STN-93
// expect: not-stone: loans: changes 't' while 'w' borrows 't[*]' and is used later
char nested_field_changed(std::string& t) {
  Two w{};
  w.a.text = t;
  t += "x";
  return use(w.b);
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"use", [] { std::string t = "abc"; Cursor c{t, 1}; return use(c) == 'b' ? 0 : 1; }},
    {"store", [] { std::string t = "abc"; Cursor c{}; store(c, t); return c.text.size() == 3 ? 0 : 1; }},
    {"forward", [] { std::string t = "abc"; Cursor c{}; forward(c, t); return c.text.size() == 3 ? 0 : 1; }},
    {"View::set", [] { std::string t = "abc"; View w{}; w.set(t); return w.v.size() == 3 ? 0 : 1; }},
    {"View::get", [] { std::string t = "abc"; View w{t}; return w.get() == 'a' ? 0 : 1; }},
    {"field_of_local", [] { std::string t = "abc"; return field_of_local(t) == 'a' ? 0 : 1; }},
    {"other_origin_unchanged", [] { std::string a = "abc", b = "xyz"; return other_origin_unchanged(a, b) == 'a' ? 0 : 1; }},
    {"stored_by_call", [] { std::string t = "abc"; return stored_by_call(t) == 'a' ? 0 : 1; }},
    {"stored_by_method", [] { std::string t = "abc"; return stored_by_method(t) == 'a' ? 0 : 1; }},
    {"nested_field", [] { std::string s = "abc", t = "q"; return nested_field(s, t) == 'a' ? 0 : 1; }},
    {"temporary_unused", [] { return temporary_unused() == 'x' ? 0 : 1; }})
#endif
