// `type std::initializer_list` i trust-filen gör den inte till en värdetyp
// (STN-71): med den raden skulle en lokal lista eller en returnerad lista
// godtas och dingla.
#include <initializer_list>

// rules: STN-71, STN-9
// expect: not-stone: return type std::initializer_list<int> (std::initializer_list<int> is a view of a temporary array)
std::initializer_list<int> make_list() { return {1, 2, 3}; }

// rules: STN-71, STN-14
// expect: not-stone: local variable 'l' has type std::initializer_list<int> (std::initializer_list is only allowed as a parameter)
int reassigned_local() {
  std::initializer_list<int> l = {1, 2};
  l = {3, 4};
  return *l.begin();
}

// rules: STN-71
// expect: stone: assuming
int count(std::initializer_list<int> xs) { return static_cast<int>(xs.size()); }
