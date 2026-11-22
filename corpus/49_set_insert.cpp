#include <set>

// rules: STN-60
// expect: stone: assuming std::set::insert
void add_key(std::set<int>& keys, int key) {
  keys.insert(key);
}

// set::insert ogiltigförklarar inga iteratorer: minnessäkert (STN-62). Loopen
// tar aldrig slut, och det är inte Stones sak (terminering). Ingen körning.
// rules: STN-60
// expect: stone: assuming
// loans: ok
void insert_during_iteration(std::set<int>& keys) {
  for (const int& key : keys) keys.insert(key + 1);
}

// Paret är ett lånevärde; kvar: structured bindings utanför range-for.
// rules: STN-20
// expect: not-stone: side effect inside expression
// loans: ok
bool inspect_insert_result(std::set<int>& keys, int key) {
  return keys.insert(key).second;
}
