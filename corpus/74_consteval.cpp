// Consteval-anrop (STN-80): kompilatorn har kört dem, resultatet är en
// konstant. `x < 0` på en ordning bygger std::__cmp_cat::__literal_zero.
#include <compare>

struct Rank {
  int number;
  // rules: STN-80
  // expect: stone
  auto operator<=>(const Rank& rhs) const { return number <=> rhs.number; }
};

struct Entry {
  Rank rank;
  int tag;
  // rules: STN-80
  // expect: stone: assuming std::operator==
  auto operator<=>(const Entry&) const = default;
};

// rules: STN-80
// expect: stone: assuming
bool less(const Entry& a, const Entry& b) { return a < b; }

// rules: STN-80
// expect: stone: assuming std::operator<
bool negative(int a, int b) { return (a <=> b) < 0; }

// rules: STN-80
// expect: stone
consteval int square(int x) { return x * x; }

// rules: STN-80
// expect: stone
int uses_square(int a) { return a + square(3); }

int counter = 0;

// En consteval-pekare kan peka in i statisk lagring: inte en konstant.
// rules: STN-80
// expect: not-stone
consteval int* where() { return &counter; }

// rules: STN-80
// expect: not-stone
int through_pointer() { return *where(); }
