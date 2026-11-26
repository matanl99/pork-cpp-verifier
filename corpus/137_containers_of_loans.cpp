// Behållare med lånebärande element (std::vector<Cursor>,
// std::deque<std::string_view>, std::map<int, std::string_view>): behållaren
// håller en lånemängd för alla sina element, som en struct (STN-93). En
// insättning lägger till det argumentet håller; ett element som läses håller
// behållarens mängd. Ett lån av ett element (`cs[0]`, en iterator) är något
// annat: det lånar behållarens lagring och dör när behållaren ändras.
#include <cstddef>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#define GROW(s) for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

struct Cursor {
  std::span<const char> text;
  std::size_t pos;
  // rules: STN-93
  // expect: stone: assuming type std::span
  char peek() const { return text[pos]; }
};

// --- godtas -------------------------------------------------------------------

// rules: STN-93
// expect: stone: assuming
char named_element(const std::string& s) {
  std::vector<Cursor> cs;
  Cursor c{s, 0};
  cs.push_back(c);
  return cs[0].peek();
}

// En temporär kopieras in: elementet lånar det temporären håller.
// rules: STN-93
// expect: stone: assuming
char temporary_element(const std::string& s) {
  std::vector<Cursor> cs;
  cs.push_back(Cursor{s, 0});
  return cs.front().peek();
}

// rules: STN-93
// expect: stone: assuming
char views_in_deque(const std::string& s) {
  std::deque<std::string_view> d;
  d.push_back(s);
  return d.front()[0];
}

// rules: STN-93
// expect: stone: assuming
char views_in_map(const std::string& s) {
  std::map<int, std::string_view> m;
  m.insert({1, s});
  m.emplace(2, s);
  return m.at(1)[0];
}

// rules: STN-93
// expect: stone: assuming
char range_for(const std::string& s, const std::string& t) {
  std::vector<std::string_view> vs;
  vs.push_back(s);
  vs.push_back(t);
  char out = 0;
  for (std::string_view v : vs) out = v[0];
  return out;
}

// En ändring av något annat än ursprunget.
// rules: STN-93
// expect: stone: assuming
char other_changed(const std::string& s, std::string& t, int n) {
  std::list<std::string_view> l;
  l.push_back(s);
  GROW(t);
  return l.front()[0];
}

// --- avvisas ------------------------------------------------------------------

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'd' borrows 's[*]'
char deque_origin_changed(int n) {
  std::string s = "abc";
  std::deque<std::string_view> d;
  d.push_back(s);
  GROW(s);
  return d.front()[0];
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'l' borrows 's[*]'
char list_origin_changed(int n) {
  std::string s = "abc";
  std::list<std::string_view> l;
  l.push_back(s);
  GROW(s);
  return l.front()[0];
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'm' borrows 's[*]'
char map_insert_origin_changed(int n) {
  std::string s = "abc";
  std::map<int, std::string_view> m;
  m.insert({1, s});
  GROW(s);
  return m.at(1)[0];
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'm' borrows 's[*]'
char map_emplace_origin_changed(int n) {
  std::string s = "abc";
  std::map<int, std::string_view> m;
  m.emplace(1, s);
  GROW(s);
  return m.at(1)[0];
}

// rules: STN-93
// expect: not-stone: loans: changes 's' while 'cs' borrows 's[*]'
char vector_origin_changed(int n) {
  std::string s = "abc";
  std::vector<Cursor> cs;
  cs.push_back(Cursor{s, 0});
  GROW(s);
  return cs[0].peek();
}

// Ursprunget dör medan behållaren lever.
// rules: STN-93
// expect: not-stone: loans:
char origin_dies() {
  std::deque<std::string_view> d;
  {
    std::string s = "abcxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    d.push_back(s);
  }
  return d.front()[0];
}

// En vy av en temporär sträng: elementet dinglar direkt.
// rules: STN-93
// expect: not-stone: loans:
char temporary_origin() {
  std::vector<std::string_view> vs;
  vs.push_back(std::string("abcxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"));
  return vs[0][0];
}

// Ett element som tilldelas ett nytt ursprung: mängden växer.
// rules: STN-93
// expect: not-stone: loans: changes 't' while 'cs' borrows 't[*]'
char element_reassigned(const std::string& s, int n) {
  std::string t = "q";
  std::vector<Cursor> cs;
  cs.push_back(Cursor{s, 0});
  cs[0] = Cursor{t, 0};
  GROW(t);
  return cs[0].peek();
}

// En referens till elementet: ändringen av behållaren flyttar elementet.
// rules: STN-93
// expect: not-stone: loans: changes 'cs' while 'c' borrows 'cs[*]'
char element_reference_across_push(const std::string& s, int n) {
  std::vector<Cursor> cs;
  cs.push_back(Cursor{s, 0});
  const Cursor& c = cs[0];
  for (int i = 0; i < n; ++i) cs.push_back(Cursor{s, 0});
  return c.peek();
}

// En konverterande tilldelning bygger elementet ur högerledet: det lånar
// det högerledet betecknar.
// rules: STN-93
// expect: not-stone: loans: changes 's' while 'o' borrows 's[*]'
char converting_assignment(int n) {
  std::string s = "abc";
  std::optional<std::string_view> o;
  o = s;
  GROW(s);
  return (*o)[0];
}

// Ett tal kopieras in i ett element av std-typer, men en reference_wrapper
// sparar adressen.
// rules: STN-93
// expect: not-stone: loans: 'v' borrows 'k[*]', which goes out of scope
int reference_wrapper_element() {
  std::vector<std::reference_wrapper<const int>> v;
  {
    int k = 7;
    v.emplace_back(k);
  }
  return v[0].get();
}

// rules: STN-93
// expect: not-stone: loans: 'm' borrows a temporary
int reference_wrapper_in_map() {
  std::map<int, std::reference_wrapper<const int>> m;
  {
    int k = 7;
    m.emplace(1, k);
  }
  return m.at(1).get();
}

// Kapslade behållare, med std::allocator som defaultargument.
// rules: STN-93
// expect: not-stone: loans: changes 's' while 'vv' borrows 's[*]'
char nested_containers(int n) {
  std::string s = "abc";
  std::vector<std::vector<std::string_view>> vv(1);
  vv[0].push_back(s);
  GROW(s);
  return vv[0][0][0];
}

// rules: STN-93
// expect: stone: assuming
char nested_ok(const std::string& s) {
  std::vector<std::vector<std::string_view>> vv(1);
  vv[0].push_back(s);
  return vv[0][0][0];
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"Cursor::peek", [] { std::string s = "ab"; Cursor c{s, 1}; return c.peek() == 'b' ? 0 : 1; }},
    {"named_element", [] { return named_element("ab") == 'a' ? 0 : 1; }},
    {"temporary_element", [] { return temporary_element("ab") == 'a' ? 0 : 1; }},
    {"views_in_deque", [] { return views_in_deque("ab") == 'a' ? 0 : 1; }},
    {"views_in_map", [] { return views_in_map("ab") == 'a' ? 0 : 1; }},
    {"range_for", [] { return range_for("ab", "cd") == 'c' ? 0 : 1; }},
    {"other_changed", [] { std::string t; return other_changed("ab", t, 5) == 'a' ? 0 : 1; }},
    {"deque_origin_changed", [] { return deque_origin_changed(5) == 'a' ? 0 : 1; }},
    {"list_origin_changed", [] { return list_origin_changed(5) == 'a' ? 0 : 1; }},
    {"map_insert_origin_changed", [] { return map_insert_origin_changed(5) == 'a' ? 0 : 1; }},
    {"map_emplace_origin_changed", [] { return map_emplace_origin_changed(5) == 'a' ? 0 : 1; }},
    {"vector_origin_changed", [] { return vector_origin_changed(5) == 'a' ? 0 : 1; }},
    {"origin_dies", [] { return origin_dies() == 'a' ? 0 : 1; }},
    {"temporary_origin", [] { return temporary_origin() == 'a' ? 0 : 1; }},
    {"element_reassigned", [] { return element_reassigned("ab", 5) == 'q' ? 0 : 1; }},
    {"element_reference_across_push", [] { return element_reference_across_push("ab", 100) == 'a' ? 0 : 1; }},
    {"converting_assignment", [] { return converting_assignment(5) == 'a' ? 0 : 1; }},
    {"reference_wrapper_element", [] { return reference_wrapper_element() == 7 ? 0 : 1; }},
    {"reference_wrapper_in_map", [] { return reference_wrapper_in_map() == 7 ? 0 : 1; }},
    {"nested_containers", [] { return nested_containers(5) == 'a' ? 0 : 1; }},
    {"nested_ok", [] { return nested_ok("ab") == 'a' ? 0 : 1; }})
#endif
