// Ett element i behållaren som värde till push_back, insert och resize
// (STN-62, std.txt `copies`): [sequence.reqmts] har inget förvillkor att t
// inte refererar in i a, så biblioteket läser värdet innan det ändrar
// behållaren (LWG 526). a.assign(n, t) har det förvillkoret och avvisas, och
// emplace_back(args&&...) står utan rad (strängare).

#include <vector>

// rules: STN-62
// expect: stone: assuming std::vector::empty, std::vector::operator[], std::vector::push_back, type std::allocator, type std::vector
void push_own(std::vector<int>& v) {
  if (!v.empty()) v.push_back(v[0]);
}

// rules: STN-62
// expect: stone: assuming std::vector::begin, std::vector::empty, std::vector::insert, std::vector::operator[], type std::allocator, type std::vector
void insert_own(std::vector<int>& v) {
  if (!v.empty()) v.insert(v.begin(), 30, v[0]);
}

// rules: STN-62
// expect: stone: assuming std::vector::empty, std::vector::operator[], std::vector::resize, std::vector::size, type std::allocator, type std::vector
void resize_own(std::vector<int>& v) {
  if (!v.empty()) v.resize(40, v[v.size() - 1]);
}

// rules: STN-62
// expect: not-stone: loans: operands 'v' and 'v[*]' of the call to 'std::vector<int>::assign' may be the same object and one is changed (line 30)
void bad_assign_own(std::vector<int>& v) {
  if (!v.empty()) v.assign(5, v[0]);
}

// rules: STN-62
// expect: not-stone: loans: changes 'v' while 'r' borrows 'v[*]' and is used later (line 38)
int bad_keep(std::vector<int>& v) {
  if (v.empty()) return 0;
  int& r = v[0];
  v.push_back(r);
  return r;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"push_own", [] { std::vector<int> v{7}; for (int i = 0; i < 20; ++i) push_own(v); return v.back() == 7 ? 0 : 1; }},
    {"insert_own", [] { std::vector<int> v{7, 8}; insert_own(v); return v.size() == 32 && v[29] == 7 ? 0 : 1; }},
    {"resize_own", [] { std::vector<int> v{7, 8}; resize_own(v); return v.size() == 40 && v[39] == 8 ? 0 : 1; }})
#endif
