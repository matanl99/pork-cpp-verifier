#include <map>
#include <vector>

// rules: STN-59
// expect: stone: assuming std::map::operator[]
void append(std::map<int, std::vector<int>>& index, int key) {
  auto& entries = index[key];
  entries.push_back(key);
}

// rules: STN-59
// expect: stone: assuming std::map::at
int lookup(const std::map<int, int>& index, int key) {
  const auto& value = index.at(key);
  return value;
}

// rules: STN-59
// expect: not-stone: changes 'index' while 'value' borrows
int invalidated(std::map<int, int>& index, int key) {
  const auto& value = index.at(key);
  index.clear();
  return value;
}

// rules: STN-59
// expect: not-stone: side effect inside expression
int insertion_in_expression(std::map<int, int>& index, int key) {
  return index[key] + 1;
}
