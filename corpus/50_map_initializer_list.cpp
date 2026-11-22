#include <initializer_list>
#include <map>
#include <string>

// rules: STN-61
// expect: stone: assuming
std::map<int, std::string> names(int key) {
  return {{key, "ett"}, {key, "två"}};
}

// rules: STN-61
// expect: not-stone: side effect inside expression
std::map<int, int> side_effect(int key) {
  return {{++key, 1}};
}

// rules: STN-61
// expect: not-stone
int list_local(int x) {
  auto values = {x, 2};
  return values.size();
}


// rules: STN-61
// expect: stone: assuming
std::map<int, int> empty_map() {
  return std::map<int, int>({});
}
