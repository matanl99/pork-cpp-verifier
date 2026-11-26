// Lån genom ett ägande element (STN-91, STN-62): det en std::unique_ptr i en
// behållare äger flyttas inte när behållaren omallokerar. Insättning och
// reserve behåller de befintliga elementens värden ([sequence.reqmts]), så ett
// lån av *v[0] lever, men inte ett lån av v[0] självt. Det som förstör ett
// element (erase, clear, pop_back, resize, tilldelning, reset) avslutar lånet.
#include <memory>
#include <utility>
#include <vector>

struct Inner {
  int value;
};

struct R {
  int value;
  Inner inner;
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

// Ett fält i det elementet äger (`v[0]->value`, `v[*][*].value`): den
// hopslagna nivån sitter vid elementets [*], eftersom bara fält följer.
// rules: STN-91
// expect: stone: assuming
int field_survives_push_back(Owners& v) {
  int& x = v[0]->value;
  v.push_back(std::make_unique<R>(R{0}));
  return x;
}

// rules: STN-91
// expect: stone: assuming
int nested_field_survives_push_back(Owners& v) {
  int& x = v.front()->inner.value;
  v.emplace_back(std::make_unique<R>(R{0}));
  add(v);
  return x;
}

// --- underkänns -------------------------------------------------------------

// Ett fält i elementet självt (utan unique_ptr) flyttas med elementet.
// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'x' borrows 'v[*].value'
int field_of_plain_element(std::vector<R>& v) {
  int& x = v[0].value;
  v.push_back(R{0});
  return x;
}

// rules: STN-91, STN-62
// expect: not-stone: loans: changes 'v' while 'x' borrows 'v[*].value[*]'
int field_across_clear(Owners& v) {
  int& x = v[0]->value;
  v.clear();
  v.push_back(std::make_unique<R>(R{0}));
  return x;
}

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

// En egen funktion: sammanfattningen säger att varje ändring av v behåller
// elementens värden (mer i corpus/130_summary_write_kinds.cpp).
// rules: STN-91, STN-62
// expect: stone: assuming
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
    {"survives_insert", [] { Owners v = two(); return survives_insert(v) == 5 ? 0 : 1; }},
    {"through_own_function", [] { Owners v = two(); return through_own_function(v) == 5 ? 0 : 1; }},
    {"field_survives_push_back", [] { Owners v = two(); return field_survives_push_back(v) == 5 ? 0 : 1; }},
    {"nested_field_survives_push_back", [] {
       Owners v = two();
       v[0]->inner.value = 7;
       return nested_field_survives_push_back(v) == 7 ? 0 : 1;
     }},
    {"field_of_plain_element", [] { std::vector<R> v{R{5}}; return field_of_plain_element(v) == 5 ? 0 : 1; }},
    {"field_across_clear", [] { Owners v = two(); return field_across_clear(v) == 5 ? 0 : 1; }})
#endif
