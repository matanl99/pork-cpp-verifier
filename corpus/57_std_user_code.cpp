#include <algorithm>
#include <map>
#include <optional>
#include <vector>

// Egen kod som körs inuti en betrodd std-funktion (STN-67). Att lita på
// std::vector::emplace_back är att lita på libstdc++, inte på konstruktorn den
// anropar. Hittat när konstruktorer blev Stone (STN-66).

int global_seed = 0;

struct Good {
  int v;
  // rules: STN-66
  // expect: stone
  explicit Good(int a) : v(a) {}
};

struct Bad {
  int v;
  // rules: STN-66, STN-22
  // expect: not-stone: uses variable 'global_seed' with static storage
  explicit Bad(int a) : v(a + global_seed) {}
};

// rules: STN-67
// expect: stone: assuming
void add_good(std::vector<Good>& v, int a) { v.emplace_back(a); }

// rules: STN-67
// expect: not-stone: calls non-stone 'Bad::Bad'
void add_bad(std::vector<Bad>& v, int a) { v.emplace_back(a); }

// rules: STN-67
// expect: not-stone: calls non-stone 'Bad::Bad'
void set_bad(std::optional<Bad>& o, int a) { o.emplace(a); }

// En egen operator< som std::map anropar.
struct Key {
  int k;
  // rules: STN-22
  // expect: not-stone: uses variable 'global_seed' with static storage
  bool operator<(const Key& o) const { return k + global_seed < o.k; }
};

// rules: STN-67
// expect: not-stone: calls non-stone 'Key::operator<'
int lookup(const std::map<Key, int>& m, Key k) { return m.count(k) > 0 ? 1 : 0; }

// En egen operator== som std::find anropar.
struct Item {
  int id;
  // rules: STN-22
  // expect: stone
  bool operator==(const Item& o) const { return id == o.id; }
};

// rules: STN-67
// expect: stone: assuming
bool has(const std::vector<Item>& v, Item x) { return std::find(v.begin(), v.end(), x) != v.end(); }

struct BadItem {
  int id;
  // rules: STN-22
  // expect: not-stone: uses variable 'global_seed' with static storage
  bool operator==(const BadItem& o) const { return id == o.id + global_seed; }
};

// rules: STN-67
// expect: not-stone: calls non-stone 'BadItem::operator=='
bool has_bad(const std::vector<BadItem>& v, BadItem x) { return std::find(v.begin(), v.end(), x) != v.end(); }
