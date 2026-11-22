// Nästlade std::initializer_list (STN-71): de inre listorna är element i den
// yttre listans temporära array, och deras arrayer lever och dör med den.
// Lånemodellen ser dem som ägd lagring under parametern (som en
// vector<vector<int>>).
#include <initializer_list>
#include <vector>

using Nested = std::initializer_list<std::initializer_list<int>>;

struct Grid {
  std::vector<std::vector<int>> rows;
  // rules: STN-71
  // expect: stone: assuming
  Grid(Nested input) {
    rows.reserve(input.size());
    for (const auto& row : input) rows.emplace_back(row);
  }
};

// rules: STN-71
// expect: stone: assuming
int use_grid() {
  Grid grid{{1, 6}, {9, 3}};
  return grid.rows[0][0] + grid.rows[1][0];
}

// rules: STN-71
// expect: stone: assuming
int sum(Nested in) {
  int s = 0;
  for (const auto& row : in)
    for (int x : row) s = s + x;
  return s;
}

// rules: STN-71
// expect: stone: assuming
int use_sum() { return sum({{1, 2}, {3}}); }

// rules: STN-71
// expect: stone: assuming
int flat(std::initializer_list<int> l) { return static_cast<int>(l.size()); }

// rules: STN-71
// expect: stone: assuming
int pass_inner(Nested in) {
  int s = 0;
  for (const auto& row : in) s = s + flat(row);
  return s;
}

// rules: STN-71
// expect: stone: assuming
int deep(std::initializer_list<std::initializer_list<std::initializer_list<int>>> in) {
  return static_cast<int>(in.size());
}

// rules: STN-71
// expect: stone: assuming
int use_deep() { return deep({{{1}}, {{2, 3}}}); }

// rules: STN-71, STN-62
// expect: stone: assuming
const int* first_of(Nested in) {
  for (const auto& row : in) return row.begin();
  return nullptr;
}

// --- avvisas -----------------------------------------------------------------

// Pekaren pekar in i en inre array, som dör vid uttryckets slut. Fanns
// under arbetet: den inre arrayen var ett okänt yttre objekt.
// rules: STN-71, STN-62
// expect: not-stone: loans: 'p' borrows a temporary
int dangling() {
  const int* p = first_of({{1, 2}});
  return p == nullptr ? 0 : *p;
}

// En inre lista som kopia är en lokal initializer_list.
// rules: STN-71
// expect: not-stone: is a view of a temporary array
int copy_inner(Nested in) {
  int s = 0;
  for (std::initializer_list<int> row : in) s = s + flat(row);
  return s;
}

// rules: STN-71
// expect: not-stone: is a view of a temporary array
const std::initializer_list<int>& pick(Nested in) { return *in.begin(); }

int g = 0;

// rules: STN-71
// expect: not-stone: static storage
int global_element() { return sum({{1, g}}); }
