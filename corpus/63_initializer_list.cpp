// std::initializer_list som parameter i egen kod (STN-71). Listan är en vy
// av anroparens temporära array: den lever hela anropet, men inte längre.
// Fallen kommer från en egen, opublicerad samling stilexempel (007, 016-018)
// och en genomgång av hur en lista kan överleva sin array.
#include <initializer_list>
#include <vector>

// --- godtas -----------------------------------------------------------------

// rules: STN-71
// expect: stone: assuming std::initializer_list::begin, std::initializer_list::end
int sum(std::initializer_list<int> xs) {
  int s = 0;
  for (int x : xs) s = s + x;
  return s;
}

// rules: STN-71
// expect: stone: assuming
int sum_by_reference(std::initializer_list<int> xs) {
  int s = 0;
  for (const int& x : xs) s = s + x;
  return s;
}

// rules: STN-71
// expect: stone: assuming std::initializer_list::size
int count(std::initializer_list<int> xs) { return static_cast<int>(xs.size()); }

// rules: STN-71
// expect: stone: assuming
int call_with_list() { return sum({1, 2, 3}); }

// rules: STN-71
// expect: stone: assuming
int call_with_empty_list() { return sum({}); }

// rules: STN-71
// expect: stone: assuming
int forward(std::initializer_list<int> xs) { return sum(xs); }

// rules: STN-71
// expect: stone: assuming
int call_forward() { return forward({4, 5}); }

struct Data {
  std::vector<int> values;
  // rules: STN-71, STN-66
  // expect: stone: assuming
  explicit Data(std::initializer_list<int> input) : values(input) {}
};

// rules: STN-71, STN-66
// expect: stone: assuming
int construct_with_braces() {
  Data d{1, 2};
  return d.values.back();
}

// rules: STN-71, STN-66
// expect: stone: assuming
int construct_with_parenthesised_list() {
  Data d({1, 2});
  return d.values.back();
}

// rules: STN-71
// expect: stone: assuming
std::vector<int> to_vector(std::initializer_list<int> xs) { return std::vector<int>(xs); }

// rules: STN-71
// expect: stone: assuming
int assign_to_vector(std::initializer_list<int> xs) {
  std::vector<int> v;
  v = xs;
  return static_cast<int>(v.size());
}

// rules: STN-71
// expect: stone: assuming
int rows_of_vectors(std::initializer_list<std::vector<int>> rows) {
  int s = 0;
  for (const std::vector<int>& r : rows) s = s + static_cast<int>(r.size());
  return s;
}

// rules: STN-71
// expect: stone: assuming
int call_rows_of_vectors() { return rows_of_vectors({{1, 2}, {3}}); }

// En pekare in i listan inom anropet: arrayen lever.
// rules: STN-71, STN-62
// expect: stone: assuming
int pointer_inside(std::initializer_list<int> xs) {
  const int* p = xs.begin();
  return p != xs.end() ? *p : 0;
}

// rules: STN-71, STN-11
// expect: stone: assuming
const int* first_pointer(std::initializer_list<int> xs) { return xs.size() > 0 ? xs.begin() : nullptr; }

// Pekaren används i samma fullständiga uttryck som arrayen.
// rules: STN-71, STN-62
// expect: stone: assuming
int use_pointer_at_once() { return *first_pointer({1, 2}); }

// --- avvisas ----------------------------------------------------------------

// Pekaren överlever anroparens temporära array.
// rules: STN-71, STN-62
// expect: not-stone: loans: 'p' borrows a temporary that is destroyed at the end of the full-expression
int pointer_outlives_list() {
  const int* p = first_pointer({1, 2});
  return p != nullptr ? *p : 0;
}

// rules: STN-71, STN-62
// expect: not-stone: loans: 'p' borrows a temporary that is destroyed at the end of the full-expression
int pointer_into_temporary_list() {
  const int* p = std::initializer_list<int>{1, 2}.begin();
  return *p;
}

// rules: STN-71, STN-10
// expect: not-stone: returns a reference that is not a field of *this or a const& parameter
const int& first_reference(std::initializer_list<int> xs) { return *xs.begin(); }

// rules: STN-71, STN-9
// expect: not-stone: return type std::initializer_list<int> (std::initializer_list<int> is a view of a temporary array)
std::initializer_list<int> make_list() { return {1, 2, 3}; }

// Arrayen i `{3, 4}` dör efter satsen, l pekar kvar på den.
// rules: STN-71, STN-14
// expect: not-stone: local variable 'l' has type std::initializer_list<int> (std::initializer_list is only allowed as a parameter)
int reassigned_local() {
  std::initializer_list<int> l = {1, 2};
  l = {3, 4};
  return *l.begin();
}

// Säkert i C++ (arrayens livstid förlängs), men en lokal lista godtas inte.
// rules: STN-71, STN-14
// expect: not-stone: local variable 'l' has type std::initializer_list<int> (std::initializer_list is only allowed as a parameter)
int local_list() {
  std::initializer_list<int> l = {1, 2};
  return static_cast<int>(l.size());
}

// rules: STN-71, STN-14
// expect: not-stone: local variable 'l' has type std::initializer_list<int> (std::initializer_list is only allowed as a parameter)
int pointer_out_of_block() {
  const int* p = nullptr;
  {
    std::initializer_list<int> l = {7};
    p = l.begin();
  }
  return *p;
}

struct HoldsList {
  std::initializer_list<int> l;
};

// rules: STN-71, STN-2
// expect: not-stone: field 'l' of HoldsList has type std::initializer_list<int>
int list_as_field() {
  HoldsList h{{1, 2}};
  return static_cast<int>(h.l.size());
}

// rules: STN-71, STN-40
// expect: not-stone: lambda captures 'xs' of type std::initializer_list<int>
int captured_by_value(std::initializer_list<int> xs) {
  auto f = [xs]() { return static_cast<int>(xs.size()); };
  return f();
}

// rules: STN-71, STN-40
// expect: not-stone: lambda captures 'xs' of type std::initializer_list<int>
int captured_by_reference(std::initializer_list<int> xs) {
  auto f = [&xs]() { return static_cast<int>(xs.size()); };
  return f();
}

// rules: STN-71
// expect: not-stone: parameter 'ps' has type std::initializer_list<int *>
int list_of_pointers(std::initializer_list<int*> ps) { return static_cast<int>(ps.size()); }

int counter = 0;

// rules: STN-71, STN-22
// expect: not-stone: uses variable 'counter' with static storage
int element_reads_global() { return sum({counter, 1}); }

struct Widget {
  int v = 0;
  // rules: STN-22
  // expect: not-stone: uses variable 'counter' with static storage
  Widget(int x) : v(x + counter) {}
};

// rules: STN-71
// expect: stone: assuming
int sum_widgets(std::initializer_list<Widget> ws) {
  int s = 0;
  for (const Widget& w : ws) s = s + w.v;
  return s;
}

// Elementen konverteras med en konstruktor som inte är Stone (G1).
// rules: STN-71, STN-23
// expect: not-stone: calls non-stone 'Widget::Widget'
int element_conversion() { return sum_widgets({1, 2}); }

// Nästlade listor: se 78_nested_initializer_lists.
// rules: STN-71
// expect: stone: assuming
int nested(std::initializer_list<std::initializer_list<int>> rows) { return static_cast<int>(rows.size()); }
