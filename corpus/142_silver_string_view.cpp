// pork-args: --silver --vector-bound 4
//
// std::string_view i Silver (SLV-45): `{size, const char* data}` som pekar in
// i källan, som std::span<const T> (SLV-26). En vy skapas från en
// strängliteral, en std::string som är en väg eller en annan vy. Källan ska ha
// samma livstid i C som i C++: en lokal variabel, en värdeparameter, en
// icke-const referensparameter eller *this i en icke-const metod. En
// `const std::string&`-parameter och *this i en const-metod är värden (kopior)
// i C, så en vy in i dem pekade på kopian: de avböjs. Lånen kontrolleras av
// Stone (STN-62), indexen av Silver.

#include <string>
#include <string_view>
#include <pork/annotations.hpp>

// rules: SLV-45
// expect: silver: assuming
unsigned long length_of(std::string_view v) { return v.size() + v.length(); }

// rules: SLV-45
// expect: silver: assuming
bool is_empty(std::string_view v) { return v.empty(); }

// rules: SLV-45, SLV-17
// expect: silver: assuming
char first_checked(std::string_view v) {
  PORK_PRE(!v.empty());
  return v[0];
}

// rules: SLV-45
// expect: stone: not silver: precondition of 'std::string_view::operator[]': index < size()
char first(std::string_view v) { return v[0]; }

// rules: SLV-45
// expect: stone: not silver: precondition of 'std::string_view::back': !empty()
char last(std::string_view v) { return v.back(); }

// rules: SLV-45
// expect: silver: assuming
std::string_view prefix(std::string_view v) { return v.substr(0, 3); }

// rules: SLV-45
// expect: stone: not silver: precondition of 'std::string_view::substr': pos <= size()
std::string_view drop(std::string_view v, unsigned long k) { return v.substr(k); }

// rules: SLV-45, SLV-17
// expect: silver: assuming
std::string_view drop_checked(std::string_view v, unsigned long k) {
  PORK_PRE(k <= v.size());
  return v.substr(k);
}

// rules: SLV-45
// expect: silver: assuming
int count_a(std::string_view v) {
  int n = 0;
  for (char c : v)
    if (c == 'a') ++n;
  return n;
}

// rules: SLV-45
// expect: silver: assuming
bool same(std::string_view a, std::string_view b) { return a == b; }

// rules: SLV-45, SLV-35
// expect: silver: assuming
std::string copy_view(std::string_view v) { return std::string(v); }

// rules: SLV-45
// expect: silver: assuming
int from_local() {
  std::string s = "abcd";
  return int(length_of(s));
}

// rules: SLV-45, SLV-28
// expect: silver: assuming
int from_literal() { return int(length_of("abc")); }

// rules: SLV-45
// expect: silver: assuming
bool literal_compare(std::string_view v) { return v == "abc"; }

// rules: SLV-45
// expect: silver: assuming
char local_view() {
  std::string s = "abcd";
  std::string_view v = s;
  v.remove_prefix(2);
  return v[0];
}

// rules: SLV-45
// expect: stone: not silver: precondition of 'std::string_view::remove_prefix': n <= size()
char remove_too_much() {
  std::string s = "abcd";
  std::string_view v = s;
  v.remove_prefix(5);
  return char(v.size());
}

// Ett element skrivs medan vyn lever: vyn ser ändringen (ingen omallokering).
// rules: SLV-45, STN-62
// expect: silver: assuming
bool edit_seen() {
  std::string o = "abc";
  std::string_view v = o;
  o[0] = 'X';
  return v == "Xbc";
}

// rules: SLV-45
// expect: silver: assuming
std::string_view borrow_mutable(std::string& s) { return s; }

// rules: SLV-45
// expect: stone: not silver: cannot translate to C: std::string_view into a const std::string& parameter
std::string_view borrow(const std::string& s) { return s; }

// rules: SLV-45
// expect: stone: not silver: cannot translate to C: std::string_view into a const std::string& parameter
unsigned long view_of_const_ref(const std::string& s) { return length_of(s); }

struct Doc {
  std::string text;
  // rules: SLV-45
  // expect: stone: not silver: cannot translate to C: std::string_view into *this of a const method
  std::string_view view() const { return text; }
  // rules: SLV-45
  // expect: silver: assuming
  std::string_view edit() { return text; }
};

// rules: SLV-45
// expect: silver: assuming
int caller() {
  std::string owner = "abcd";
  std::string_view borrowed = borrow_mutable(owner);
  std::string_view slice = prefix(borrowed);
  std::string copy = copy_view(slice);
  owner[0] = 'X';
  return slice == "Xbc" && copy == "abc" ? 0 : 1;
}

// Modellen räknar rätt: eftervillkoren bevisas (och ett fel underkänns).
// rules: SLV-45, SLV-18
// expect: silver: assuming
int checked_caller() {
  PORK_POST(int r, r == 0);
  return caller();
}

// rules: SLV-45, SLV-18
// expect: stone: not silver: postcondition of 'wrong_caller'
int wrong_caller() {
  PORK_POST(int r, r == 1);
  return caller();
}

// rules: SLV-45, SLV-18
// expect: silver: assuming
int count_in_literal() {
  PORK_POST(int r, r == 2);
  return count_a("abca");
}

// rules: SLV-45, SLV-18
// expect: silver: assuming
unsigned long prefix_length(std::string_view v) {
  PORK_POST(unsigned long r, r <= 3 && r <= v.size());
  return prefix(v).size();
}

// rules: SLV-45, SLV-18
// expect: silver: assuming
bool copy_is_equal(std::string_view v) {
  PORK_POST(bool r, r);
  std::string s(v);
  std::string_view w = s;
  return w == v;
}

// rules: SLV-45
// expect: stone: not silver: std::string_view longer than 4 characters
unsigned long long_literal() { return length_of("abcdef"); }

// Range-for över en vy (STN-62): det vyn lånar får inte ändras i loopen.
// rules: STN-62, SLV-45
// expect: not-stone: loans:
int grow_while_iterating(std::string& s) {
  std::string_view v = s;
  int n = 0;
  for (char c : v) {
    if (c == 'a') s += "b";
    ++n;
  }
  return n;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"length_of", [] { return length_of("abc") == 6 ? 0 : 1; }},
    {"is_empty", [] { return is_empty("") && !is_empty("a") ? 0 : 1; }},
    {"first_checked", [] { return first_checked("abc") == 'a' ? 0 : 1; }},
    {"prefix", [] { return prefix("abcdef") == "abc" && prefix("ab") == "ab" ? 0 : 1; }},
    {"drop_checked", [] { return drop_checked("abcdef", 2) == "cdef" && drop_checked("ab", 2).empty() ? 0 : 1; }},
    {"count_a", [] { return count_a("banana") == 3 ? 0 : 1; }},
    {"same", [] { return same("ab", "ab") && !same("ab", "abc") ? 0 : 1; }},
    {"copy_view", [] { return copy_view("abc") == "abc" ? 0 : 1; }},
    {"from_local", [] { return from_local() == 8 ? 0 : 1; }},
    {"from_literal", [] { return from_literal() == 6 ? 0 : 1; }},
    {"literal_compare", [] { return literal_compare("abc") && !literal_compare("abd") ? 0 : 1; }},
    {"local_view", [] { return local_view() == 'c' ? 0 : 1; }},
    {"edit_seen", [] { return edit_seen() ? 0 : 1; }},
    {"borrow_mutable", [] { std::string s = "abc"; return borrow_mutable(s) == "abc" ? 0 : 1; }},
    {"borrow", [] { std::string s = "abc"; return borrow(s) == "abc" ? 0 : 1; }},
    {"view_of_const_ref", [] { return view_of_const_ref("abc") == 6 ? 0 : 1; }},
    {"caller", [] { return caller(); }},
    {"checked_caller", [] { return checked_caller(); }},
    {"count_in_literal", [] { return count_in_literal() == 2 ? 0 : 1; }},
    {"prefix_length", [] { return prefix_length("abcdef") == 3 && prefix_length("a") == 1 ? 0 : 1; }},
    {"copy_is_equal", [] { return copy_is_equal("abc") ? 0 : 1; }},
    {"long_literal", [] { return long_literal() == 12 ? 0 : 1; }})
#endif
