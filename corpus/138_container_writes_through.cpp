// En std-behållares egna metoder (push_back, insert, clear ...) kopierar,
// flyttar och förstör element, men skriver inte genom det elementen lånar,
// när elementens kopiering och destruktor inte kör användarkod (std-typer,
// eller trivialt kopierbara typer med trivial destruktor, STN-93). Samma
// ursprung får då sättas in flera gånger. Element med egen kod kan skriva
// genom sina lån, och räknas som förut.
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Cursor {
  std::span<const char> text;
  std::size_t pos;
};

// En destruktor som skriver genom elementets lån.
struct Clearing {
  std::span<int> data;
  // rules: STN-68
  // expect: stone: assuming type std::span
  ~Clearing() {
    if (!data.empty()) data[0] = 0;
  }
};

// --- godtas -------------------------------------------------------------------

// rules: STN-93, STN-62
// expect: stone: assuming
char same_origin_twice(const std::string& s) {
  std::vector<std::string_view> vs;
  vs.push_back(s);
  vs.push_back(s);
  return vs[1][0];
}

// rules: STN-93, STN-62
// expect: stone: assuming
char same_origin_in_loop(std::string& s, int n) {
  std::vector<std::string_view> vs;
  for (int i = 0; i < n; ++i) vs.push_back(s);
  return vs.empty() ? 'x' : vs[0][0];
}

// rules: STN-93, STN-62
// expect: stone: assuming
void add(std::vector<std::string_view>& out, const std::string& s) { out.push_back(s); }

// rules: STN-93, STN-62
// expect: stone: assuming
std::size_t cursors(const std::string& s) {
  std::vector<Cursor> cs;
  cs.push_back(Cursor{s, 0});
  cs.push_back(Cursor{s, 1});
  return cs.size();
}

// rules: STN-93, STN-62
// expect: stone: assuming
int spans_twice(std::vector<int>& v) {
  std::vector<std::span<int>> ss;
  ss.push_back(v);
  ss.push_back(v);
  ss.clear();
  return static_cast<int>(v.size());
}

// --- avvisas ------------------------------------------------------------------

// Elementets destruktor kan skriva genom lånet medan v ändras.
// rules: STN-93, STN-62
// expect: not-stone: loans:
int destructor_writes_through(std::vector<int>& v) {
  std::vector<Clearing> cs;
  cs.push_back(Clearing{v});
  cs.push_back(Clearing{v});
  return static_cast<int>(v.size());
}

// Ursprunget ändras medan behållaren lever.
// rules: STN-93
// expect: not-stone: loans: changes 's' while 'vs' borrows 's[*]'
char origin_changed(std::string& s, int n) {
  std::vector<std::string_view> vs;
  vs.push_back(s);
  vs.push_back(s);
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return vs[0][0];
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"Clearing::~Clearing", [] { std::vector<int> v{1}; { Clearing c{v}; } return v[0] == 0 ? 0 : 1; }},
    {"same_origin_twice", [] { return same_origin_twice("ab") == 'a' ? 0 : 1; }},
    {"same_origin_in_loop", [] { std::string s = "ab"; return same_origin_in_loop(s, 3) == 'a' ? 0 : 1; }},
    {"add", [] { std::vector<std::string_view> o; std::string s = "ab"; add(o, s); return o[0][0] == 'a' ? 0 : 1; }},
    {"cursors", [] { return cursors("ab") == 2 ? 0 : 1; }},
    {"spans_twice", [] { std::vector<int> v{1}; return spans_twice(v) == 1 ? 0 : 1; }},
    {"destructor_writes_through", [] { std::vector<int> v{1}; return destructor_writes_through(v) == 1 ? 0 : 1; }},
    {"origin_changed", [] { std::string s = "ab"; return origin_changed(s, 5) == 'a' ? 0 : 1; }})
#endif
