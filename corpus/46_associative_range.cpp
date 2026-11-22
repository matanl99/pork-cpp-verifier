#include <map>
#include <set>

// rules: STN-35
// expect: stone: assuming std::get
int sum_map(const std::map<int, int>& m) {
  int sum = 0;
  for (const auto& [key, value] : m) sum += key + value;
  return sum;
}

// rules: STN-35
// expect: stone: assuming type std::allocator
int sum_set(const std::set<int>& s) {
  int sum = 0;
  for (int value : s) sum += value;
  return sum;
}

// rules: STN-35
// expect: not-stone: requires a read-only element reference
void mutate_element(std::map<int, int>& m) {
  for (auto& entry : m) entry.second = 0;
}

// rules: STN-35
// expect: not-stone: changes 'm' while the range-for iterator borrows
void mutate_map(std::map<int, int>& m) {
  for (const auto& entry : m) m.clear();
}

// rules: STN-35
// expect: not-stone: requires the container's element type
int converted_elements(const std::set<int>& s) {
  int count = 0;
  for (long value : s) count += value != 0;
  return count;
}
