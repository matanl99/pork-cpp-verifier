// Erase-remove-idiomet (STN-85): c.erase(std::remove_if(c.begin(), c.end(), p), c.end()).
#include <algorithm>
#include <vector>

struct Order {
  int units;
  bool cancelled;
};

struct Basket {
  std::vector<int> items;
};

// --- godtas -----------------------------------------------------------------

// rules: STN-85, STN-79
// expect: stone: assuming
void drop_cancelled(std::vector<Order>& orders) {
  orders.erase(std::remove_if(orders.begin(), orders.end(), [](const Order& o) { return o.cancelled; }),
               orders.end());
}

// rules: STN-85
// expect: stone: assuming
void drop_value(std::vector<int>& v, int x) { v.erase(std::remove(v.begin(), v.end(), x), v.end()); }

// rules: STN-85
// expect: stone: assuming
void drop_duplicates(std::vector<int>& v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
}

// rules: STN-85
// expect: stone: assuming
void drop_from_field(Basket& b) { b.items.erase(std::remove(b.items.begin(), b.items.end(), 0), b.items.end()); }

// rules: STN-85
// expect: stone: assuming
int drop_local() {
  std::vector<int> v{1, 0, 2, 0};
  v.erase(std::remove(v.begin(), v.end(), 0), v.end());
  return static_cast<int>(v.size());
}

class Cart {
 public:
  // rules: STN-85
  // expect: stone: assuming
  void drop_zeros() { items_.erase(std::remove(items_.begin(), items_.end(), 0), items_.end()); }

 private:
  std::vector<int> items_;
};

// Hela initieraren får ha en sidoeffekt (STN-21), så formen gäller där med.
// rules: STN-85
// expect: stone: assuming
bool drop_and_check(std::vector<int>& v) {
  auto rest = v.erase(std::remove(v.begin(), v.end(), 0), v.end());
  return rest == v.end();
}

// --- underkänns -------------------------------------------------------------

// Algoritmen går över en annan behållare än den som raderas.
// rules: STN-85, STN-21
// expect: not-stone: side effect inside expression
void other_container(std::vector<int>& v, std::vector<int>& w) {
  v.erase(std::remove(w.begin(), w.end(), 0), v.end());
}

// Slutet i erase kommer från en annan behållare.
// rules: STN-85, STN-21
// expect: not-stone
void other_end(std::vector<int>& v, std::vector<int>& w) {
  v.erase(std::remove(v.begin(), v.end(), 0), w.end());
}

// Bara remove, remove_if och unique: en annan algoritm är inte idiomet.
// rules: STN-85, STN-21
// expect: not-stone: side effect inside expression
void other_algorithm(std::vector<int>& v) { v.erase(std::find(v.begin(), v.end(), 0), v.end()); }

// Bara hela intervallet: c.begin() + n ingår inte i formen.
// rules: STN-85, STN-21
// expect: not-stone: side effect inside expression
void tail_only(std::vector<int>& v) { v.erase(std::remove(v.begin() + 1, v.end(), 0), v.end()); }

// erase inne i ett större uttryck: ingen av anropen får ha sidoeffekt där.
// rules: STN-85, STN-21
// expect: not-stone: side effect inside expression
bool inside_expression(std::vector<int>& v) {
  return v.erase(std::remove(v.begin(), v.end(), 0), v.end()) == v.end();
}

// Lånemodellen granskar fortfarande: erase ogiltigförklarar lånet.
// rules: STN-85, STN-62
// expect: not-stone: loans
int loan_after(std::vector<int>& v) {
  const int& first = v[0];
  v.erase(std::remove(v.begin(), v.end(), 0), v.end());
  return first;
}

// Värdet är ett element i intervallet som algoritmen skriver i: remove tar
// det som const T& och läser det medan elementen flyttas.
// rules: STN-85, STN-62
// expect: not-stone
void value_from_range(std::vector<int>& v) { v.erase(std::remove(v.begin(), v.end(), v[0]), v.end()); }

// Predikatet får inte skriva det det fångar (STN-79).
// rules: STN-85, STN-79
// expect: not-stone
int counting_predicate(std::vector<int>& v) {
  int n = 0;
  v.erase(std::remove_if(v.begin(), v.end(), [&n](int x) { n = n + 1; return x == 0; }), v.end());
  return n;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
using Vec = std::vector<int>;
PORK_RUN_CASES(
    {"drop_cancelled", [] {
       std::vector<Order> v{{1, true}, {2, false}, {3, true}};
       drop_cancelled(v);
       return v.size() == 1 && v[0].units == 2 ? 0 : 1;
     }},
    {"drop_value", [] { Vec v{1, 2, 1, 3}; drop_value(v, 1); return v == Vec{2, 3} ? 0 : 1; }},
    {"drop_duplicates", [] { Vec v{3, 1, 3, 2, 1}; drop_duplicates(v); return v == Vec{1, 2, 3} ? 0 : 1; }},
    {"drop_from_field", [] { Basket b{{0, 4, 0}}; drop_from_field(b); return b.items == Vec{4} ? 0 : 1; }},
    {"drop_local", [] { return drop_local() == 2 ? 0 : 1; }},
    {"Cart::drop_zeros", [] { Cart c; c.drop_zeros(); return 0; }},
    {"drop_and_check", [] { Vec v{0, 1}; return drop_and_check(v) ? 0 : 1; }})
#endif
