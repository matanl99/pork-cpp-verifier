// En flytt in i ett anrop förbrukar platsen (STN-57, lånemodellen): en
// icke-const T&&-parameter eller objektet i en &&-kvalificerad metod får ta
// innehållet, vad std.txt än säger om resten (`value_or : reads` gäller bara
// *this, P-d). Lån in i den ägda lagringen dör. Hittat när reglerna för lån
// stängdes av på prov (lånemodellen ensam): de två första är
// heap-use-after-free under ASan.
#include <optional>
#include <utility>
#include <vector>

// rules: STN-57
// expect: not-stone
// loans: rejected
int bad_value_or_moved_argument(const std::optional<std::vector<int>>& o, std::vector<int> v) {
  const int& r = v[0];
  int n = 0;
  {
    std::vector<int> w = o.value_or(std::move(v));
    n = static_cast<int>(w.size());
  }
  return r + n;
}

// rules: STN-57
// expect: not-stone
// loans: rejected
int bad_value_or_moved_object(std::optional<std::vector<int>> o) {
  const int& r = (*o)[0];
  int n = 0;
  {
    std::vector<int> w = std::move(o).value_or(std::vector<int>{});
    n = static_cast<int>(w.size());
  }
  return r + n;
}

// rules: STN-57
// expect: not-stone
// loans: rejected
int bad_push_moved(std::vector<std::vector<int>>& all, std::vector<int> v) {
  const int& r = v[0];
  all.push_back(std::move(v));
  all.clear();
  return r;
}

// Lånet dör före flytten.
// rules: STN-57
// expect: stone: assuming
// loans: ok
int value_or_after_last_use(const std::optional<std::vector<int>>& o, std::vector<int> v) {
  int first = v[0];
  std::vector<int> w = o.value_or(std::move(v));
  return first + static_cast<int>(w.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_value_or_moved_argument", [] { return bad_value_or_moved_argument(std::nullopt, std::vector<int>{7}) == 8 ? 0 : 1; }},
    {"bad_value_or_moved_object", [] { return bad_value_or_moved_object(std::vector<int>{1}) == 2 ? 0 : 1; }},
    {"bad_push_moved", [] { std::vector<std::vector<int>> all; return bad_push_moved(all, std::vector<int>{3}) == 3 ? 0 : 1; }},
    {"value_or_after_last_use", [] { return value_or_after_last_use(std::nullopt, std::vector<int>{7}) == 8 ? 0 : 1; }})
#endif
