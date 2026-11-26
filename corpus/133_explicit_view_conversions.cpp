// En uttrycklig konvertering till en vy (`std::span<const int>(v)`,
// `static_cast<std::string_view>(s)`) är konstruktorn den innehåller: vyn lånar
// det konstruktorn får (STN-62), som vid en implicit konvertering.
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Holder {
  std::span<const int> view;
};

// --- godtas -------------------------------------------------------------------

// rules: STN-62, STN-93
// expect: stone: assuming
int read_before_clear(std::vector<int>& v) {
  if (v.empty()) return 0;
  Holder h{std::span<const int>(v)};
  int value = h.view[0];
  v.clear();
  return value;
}

// rules: STN-62
// expect: stone: assuming
int functional_local(const std::vector<int>& v) {
  std::span<const int> s = std::span<const int>(v);
  return static_cast<int>(s.size());
}

// rules: STN-62
// expect: stone: assuming
std::size_t static_cast_view(const std::string& s) {
  std::string_view w = static_cast<std::string_view>(s);
  return w.size();
}

// --- avvisas ------------------------------------------------------------------

// rules: STN-62, STN-93
// expect: not-stone: loans: changes 'v' while 'h' borrows 'v[*]'
int read_after_clear(std::vector<int>& v, int n) {
  Holder h{std::span<const int>(v)};
  for (int i = 0; i < n; ++i) v.push_back(i);
  return h.view[0];
}

// rules: STN-62
// expect: not-stone: loans: changes 's' while 'w' borrows 's[*]'
char view_after_append(std::string& s, int n) {
  std::string_view w = static_cast<std::string_view>(s);
  for (int i = 0; i < n; ++i) s += "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
  return w[0];
}

// rules: STN-62
// expect: not-stone: loans: returns a loan of 't[*]'
std::span<const int> returns_local() {
  std::vector<int> t{1, 2};
  return std::span<const int>(t);
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"read_before_clear", [] { std::vector<int> v{4}; return read_before_clear(v) == 4 && v.empty() ? 0 : 1; }},
    {"functional_local", [] { std::vector<int> v{1, 2}; return functional_local(v) == 2 ? 0 : 1; }},
    {"static_cast_view", [] { std::string s = "abc"; return static_cast_view(s) == 3 ? 0 : 1; }},
    {"read_after_clear", [] { std::vector<int> v{4}; return read_after_clear(v, 100) == 4 ? 0 : 1; }},
    {"view_after_append", [] { std::string s = "a"; return view_after_append(s, 5) == 'a' ? 0 : 1; }},
    {"returns_local", [] { return returns_local().size() == 2 ? 0 : 1; }})
#endif
