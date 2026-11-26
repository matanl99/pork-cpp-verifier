// Lån genom ett ägande element (STN-91, STN-62): det en std::unique_ptr i en
// behållare äger flyttas inte när behållaren omallokerar. Insättning och
// reserve behåller de befintliga elementens värden ([sequence.reqmts]), så ett
// lån av *v[0] lever, men inte ett lån av v[0] självt. Det som förstör ett
// element (erase, clear, pop_back, resize, tilldelning, reset) avslutar lånet.
#include <memory>
#include <utility>
#include <vector>

struct R {
  int value;
};

using Owners = std::vector<std::unique_ptr<R>>;

// rules: STN-91
// expect: stone: assuming
void add(Owners& v) { v.push_back(std::make_unique<R>(R{0})); }

// --- godtas -----------------------------------------------------------------

// rules: STN-91
// expect: stone: assuming
int survives_push_back(int value) {
  Owners v;
  v.push_back(std::make_unique<R>(R{value}));
  R& borrowed = *v.front();
  v.push_back(std::make_unique<R>(R{0}));
  return borrowed.value;
}

// rules: STN-91
// expect: stone: assuming
int survives_emplace_back(Owners& v) {
  R& borrowed = *v[0];
  v.emplace_back(std::make_unique<R>(R{1}));
  return borrowed.value;
}

// rules: STN-91
// expect: stone: assuming
int survives_reserve(Owners& v) {
  R& borrowed = *v[0];
  v.reserve(100);
  return borrowed.value;
}

// rules: STN-91
// expect: stone: assuming
int survives_insert(Owners& v) {
  R& borrowed = *v[0];
  v.insert(v.begin(), std::make_unique<R>(R{2}));
  return borrowed.value;
}

// --- underkänns -------------------------------------------------------------

// Elementet självt flyttas.
// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'p' borrows 'v[*]' and is used later
int element_itself(Owners& v) {
  auto& p = v[0];
  v.push_back(std::make_unique<R>(R{1}));
  return p ? 1 : 0;
}

// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'borrowed' borrows 'v[*][*]' and is used later
int erased(Owners& v) {
  R& borrowed = *v[0];
  v.erase(v.begin());
  return borrowed.value;
}

// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'borrowed' borrows 'v[*][*]' and is used later
int cleared(Owners& v) {
  R& borrowed = *v[0];
  v.clear();
  return borrowed.value;
}

// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'borrowed' borrows 'v[*][*]' and is used later
int popped(Owners& v) {
  R& borrowed = *v.back();
  v.pop_back();
  return borrowed.value;
}

// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'borrowed' borrows 'v[*][*]' and is used later
int resized(Owners& v) {
  R& borrowed = *v[0];
  v.resize(0);
  return borrowed.value;
}

// rules: STN-62
// expect: not-stone: loans: changes 'v[*]' while 'borrowed' borrows 'v[*][*]' and is used later
int element_replaced(Owners& v) {
  R& borrowed = *v[0];
  v[0] = std::make_unique<R>(R{3});
  return borrowed.value;
}

// rules: STN-62
// expect: not-stone: loans: changes 'v[*]' while 'borrowed' borrows 'v[*][*]' and is used later
int element_reset(Owners& v) {
  R& borrowed = *v[0];
  v[0].reset();
  return borrowed.value;
}

// rules: STN-62
// expect: not-stone: loans: changes 'v[*]' while 'x' borrows
int field_of_replaced(Owners& v) {
  int& x = v[0]->value;
  v[0] = std::make_unique<R>(R{3});
  return x;
}

// En egen funktion: sammanfattningen säger att v ändras, inte hur.
// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'borrowed' borrows 'v[*][*]' and is used later
int through_own_function(Owners& v) {
  R& borrowed = *v[0];
  add(v);
  return borrowed.value;
}

// Ett int-element flyttas vid omallokering.
// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'b' borrows 'v[*]' and is used later
int plain_elements(std::vector<int>& v) {
  int& b = v[0];
  v.push_back(1);
  return b;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
static Owners two() {
  Owners v;
  v.push_back(std::make_unique<R>(R{5}));
  v.push_back(std::make_unique<R>(R{6}));
  return v;
}
PORK_RUN_CASES(
    {"add", [] { Owners v; add(v); return v.size() == 1 && v[0] ? 0 : 1; }},
    {"survives_push_back", [] { return survives_push_back(4) == 4 ? 0 : 1; }},
    {"survives_emplace_back", [] { Owners v = two(); return survives_emplace_back(v) == 5 ? 0 : 1; }},
    {"survives_reserve", [] { Owners v = two(); return survives_reserve(v) == 5 ? 0 : 1; }},
    {"survives_insert", [] { Owners v = two(); return survives_insert(v) == 5 ? 0 : 1; }})
#endif
