// Lambdor direkt som argument till std (STN-79), från gruppexemplet 034.
#include <algorithm>
#include <vector>

int seen = 0;

struct Planned {
  int tick{0};
  int who{0};
};

struct Value {
  int n;
  // rules: STN-79
  // expect: stone
  bool less(const Value& o) const { return n < o.n; }
  // rules: STN-79
  // expect: not-stone: static storage
  bool counted_less(const Value& o) const {
    seen = seen + 1;
    return n < o.n;
  }
};

// --- godtas -----------------------------------------------------------------

// rules: STN-79, STN-78
// expect: stone: assuming
void sort_by(std::vector<Value>& v) {
  std::sort(v.begin(), v.end(), [](const Value& a, const Value& b) { return a.less(b); });
}

// rules: STN-79
// expect: stone: assuming
void sort_generic(std::vector<int>& v) {
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a > b; });
}

// Fångst som kopia.
// rules: STN-79
// expect: stone: assuming
int count_above(const std::vector<int>& v, int k) {
  auto n = std::count_if(v.begin(), v.end(), [k](int x) { return x > k; });
  return static_cast<int>(n);
}

// Lambdan skriver genom sin parameter, alltså genom iteratorerna (STN-62).
// rules: STN-79, STN-78
// expect: stone: assuming
void double_all(std::vector<int>& v) {
  std::for_each(v.begin(), v.end(), [](int& x) { x = x * 2; });
}

// rules: STN-79
// expect: stone: assuming
bool has_negative(const std::vector<int>& v) {
  return std::find_if(v.begin(), v.end(), [](int x) { return x < 0; }) != v.end();
}

// Från corpus/30_gaps.cpp (lucka 2 från rugby, stängd av STN-79).
// rules: STN-79, STN-62
// expect: stone: assuming
// loans: ok
void replace_or_add(std::vector<Planned>& planned, const Planned& entry) {
  if (auto it = std::ranges::find_if(planned, [&](const Planned& p) { return p.tick == entry.tick; });
      it != planned.end())
    *it = entry;
  else
    planned.push_back(entry);
}

// En lokal lambda kopierad till std (rugby: TurnController::plan).
// rules: STN-79
// expect: stone: assuming
int count_with_variable(const std::vector<int>& v, int k) {
  auto above = [&](int x) { return x > k; };
  auto n = std::count_if(v.begin(), v.end(), above);
  return static_cast<int>(n);
}

// rules: STN-79
// expect: not-stone: writes 'sum', captured by reference
int sum_with_variable(const std::vector<int>& v) {
  int sum = 0;
  auto add = [&](int x) { sum = sum + x; };
  std::for_each(v.begin(), v.end(), add);
  return sum;
}

// Fångsten läses medan sort skriver v genom iteratorerna (G3, försiktigt).
// rules: STN-79, STN-62
// expect: not-stone: loans
void sort_reading_self(std::vector<int>& v) {
  if (v.empty()) return;
  std::sort(v.begin(), v.end(), [&v](int a, int b) { return (a < v[0]) < (b < v[0]); });
}

// En lokal referens får inte fångas (STN-40).
// rules: STN-79, STN-40
// expect: not-stone: captures local reference
void sort_around_element(std::vector<int>& v) {
  if (v.empty()) return;
  int& pivot = v[0];
  std::sort(v.begin(), v.end(), [&pivot](int a, int b) { return (a < pivot) < (b < pivot); });
}

// --- avvisas: lambdan ---------------------------------------------------------

// Lambdans anrop av egen kod som inte är Stone (gruppexemplets bad_lambda).
// rules: STN-79, STN-40
// expect: not-stone: Value::counted_less
void sort_counted(std::vector<Value>& v) {
  std::sort(v.begin(), v.end(), [](const Value& a, const Value& b) { return a.counted_less(b); });
}

// Referensfångsten läses bara.
// rules: STN-79
// expect: stone: assuming
int count_ref(const std::vector<int>& v, int k) {
  auto n = std::count_if(v.begin(), v.end(), [&k](int x) { return x > k; });
  return static_cast<int>(n);
}

// Skrivningen till sum syns inte i anropet.
// rules: STN-79
// expect: not-stone: writes 'sum', captured by reference
int sum_ref(const std::vector<int>& v) {
  int sum = 0;
  std::for_each(v.begin(), v.end(), [&](int x) { sum = sum + x; });
  return sum;
}

// rules: STN-79, STN-40
// expect: not-stone: mutable
int count_mutable(const std::vector<int>& v) {
  auto n = std::count_if(v.begin(), v.end(), [c = 0](int x) mutable { return x > c; });
  return static_cast<int>(n);
}

// rules: STN-79, STN-40
// expect: not-stone: static storage
void sort_global(std::vector<int>& v) {
  std::sort(v.begin(), v.end(), [](int a, int b) {
    seen = seen + 1;
    return a < b;
  });
}

// Bara instansen för int används; den granskas.
// rules: STN-79, STN-40
// expect: not-stone: static storage
void sort_generic_global(std::vector<int>& v) {
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
    seen = seen + 1;
    return a < b;
  });
}

// En fångad iterator är ett lån, inte ett värde (STN-40).
// rules: STN-79, STN-40
// expect: not-stone
bool find_iterator(const std::vector<int>& v) {
  if (v.empty()) return false;
  auto first = v.begin();
  return std::find_if(v.begin(), v.end(), [first](int x) { return x == *first; }) != v.end();
}

struct Holder {
  int k;
  // rules: STN-79, STN-40
  // expect: not-stone: this
  int count(const std::vector<int>& v) const {
    auto n = std::count_if(v.begin(), v.end(), [this](int x) { return x > k; });
    return static_cast<int>(n);
  }
};

// --- avvisas: inte till std ----------------------------------------------------

template <class F>
// rules: STN-79
// expect: not-stone
int apply(F f) {
  return f(1);
}

// rules: STN-79
// expect: not-stone
int to_user() { return apply([](int x) { return x + 1; }); }
