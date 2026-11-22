#include <map>
#include <set>
#include <span>
#include <vector>

// Lånevärden (STN-62): iteratorer, pekare, vyer och par som bär lån följs av
// lånemodellen i stället för av idiomregler i LocalCheck. Fallen här visar
// det som de gamla reglerna (STN-58/59/60/39/41/61) inte täckte.

// Kasserat resultat av map::insert (inte bara set::insert).
// rules: STN-60
// expect: stone: assuming
// loans: ok
void add(std::map<int, int>& m, int k) { m.insert({k, 1}); }

// Kasserat resultat av map::erase(key) och try_emplace.
// rules: STN-60
// expect: stone: assuming
// loans: ok
void replace(std::map<int, int>& m, int k) {
  m.erase(k);
  m.try_emplace(k, 2);
}

// Referens till vector::front lånar vektorn, inte något argument.
// rules: STN-59
// expect: stone: assuming
// loans: ok
int first(const std::vector<int>& v) {
  if (v.empty()) return 0;
  const int& x = v.front();
  return x;
}

// ... och den håller lånet: push_back medan referensen lever.
// rules: STN-59, STN-62
// expect: not-stone: changes 'v' while 'x' borrows
// loans: rejected: changes 'v' while 'x' borrows 'v[*]'
int first_then_grow(std::vector<int>& v) {
  const int& x = v.front();
  v.push_back(1);
  return x;
}

// Iterator från vector::begin, jämförd och avrefererad.
// rules: STN-58
// expect: stone: assuming
// loans: ok
int head(const std::vector<int>& v) {
  auto it = v.begin();
  return it == v.end() ? 0 : *it;
}

// Iteratorn ogiltigförklaras av push_back.
// rules: STN-58, STN-62
// expect: not-stone: loans: changes 'v' while 'it' borrows 'v[*]'
// loans: rejected: changes 'v' while 'it' borrows 'v[*]'
int head_then_grow(std::vector<int>& v) {
  auto it = v.begin();
  v.push_back(1);
  return *it;
}

// Pekare till ett fält i en lokal.
// rules: STN-39
// expect: stone
// loans: ok
int through_pointer(int a) {
  int x = a;
  const int* p = &x;
  return *p;
}

// Pekaren får inte överleva det den pekar på.
// rules: STN-39, STN-11
// expect: not-stone: loans: returns a loan of 'x'
// loans: rejected: returns a loan of 'x'
const int* escape(int a) {
  int x = a;
  return &x;
}

// Pekararitmetik är inget lån.
// rules: STN-39
// expect: not-stone: pointer arithmetic or ordering: +
// loans: ok
int arithmetic(int a) {
  int x = a;
  const int* p = &x;
  return *(p + 0);
}

// Lokal span av en vektor.
// rules: STN-41
// expect: stone: assuming
// loans: ok
int span_sum(const std::vector<int>& v) {
  std::span<const int> s = v;
  int n = 0;
  for (const int x : s) n = n + x;
  return n;
}

// Initializer_list till en annan std-konstruktor än map.
// rules: STN-61
// expect: stone: assuming
// loans: ok
std::vector<int> pair_of(int a, int b) { return {a, b}; }
