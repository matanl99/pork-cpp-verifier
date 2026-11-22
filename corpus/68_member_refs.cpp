// Egna metoder som ger T& in i *this (STN-76), som int& operator[] i
// gruppexemplet 024. Anroparen ser anropet som en väg till hela objektet.
#include <array>
#include <vector>

int seen = 0;

struct Slots {
  std::array<int, 2> data;
  int other = 0;
  // rules: STN-76
  // expect: stone: assuming
  int& operator[](int index) { return data[index]; }
  // rules: STN-76, STN-10
  // expect: stone: assuming
  const int& operator[](int index) const { return data[index]; }
  // rules: STN-76
  // expect: stone: assuming
  int& first_field() { return other; }
  // rules: STN-76
  // expect: stone: assuming
  int& delegated(int index) { return (*this)[index]; }
  // rules: STN-76
  // expect: stone: assuming
  int& pick(bool which) { return which ? other : other; }
  // En egen effekt på objektet: synlig, eftersom objektet skrivs.
  // rules: STN-76
  // expect: stone: assuming
  int& counted(int index) {
    other = other + 1;
    return data[index];
  }
};

struct Grow {
  std::vector<int> v;
  // rules: STN-76
  // expect: stone: assuming
  int& operator[](int i) { return v[i]; }
  // rules: STN-76
  // expect: stone: assuming
  void grow() { v.push_back(1); }
};

struct Delegating {
  std::array<int, 2> data;
  // rules: STN-10
  // expect: stone: assuming
  const int& at_index(int index) const { return data[index]; }
  // rules: STN-10
  // expect: stone: assuming
  const int& operator[](int index) const { return at_index(index); }
  // Med en const&-parameter kan resultatet peka in i argumentet.
  // rules: STN-10
  // expect: stone: assuming
  const int& either(const int& other, bool c) const { return c ? data[0] : other; }
  // rules: STN-10
  // expect: not-stone: not a field of *this
  const int& through_param(const int& other) const { return either(other, false); }
};

// --- godtas -----------------------------------------------------------------

// rules: STN-76
// expect: stone: assuming
int assign_through() {
  Slots s{{2, 3}};
  s[1] = 9;
  const Slots& view = s;
  return view[1];
}

// rules: STN-76
// expect: stone: assuming
int compound(Slots& s) {
  s[0] += 2;
  ++s[1];
  return s[0];
}

// rules: STN-76, STN-31
// expect: stone: assuming
int local_ref(Slots& s) {
  int& r = s[1];
  r = 4;
  return s[1];
}

// rules: STN-76
// expect: stone: assuming
void via_delegated(Slots& s) { s.delegated(0) = 5; }

// rules: STN-76
// expect: stone: assuming
void via_field(Slots& s) { s.first_field() = 5; }

// rules: STN-76
// expect: stone: assuming
void via_counted(Slots& s) { s.counted(0) = 1; }

// rules: STN-76
// expect: stone: assuming
void vector_field(Grow& g) {
  if (!g.v.empty()) g[0] = 3;
}

// --- avvisas i metoden ------------------------------------------------------------

struct Bad {
  int x = 0;
  // rules: STN-76
  // expect: not-stone: into *this
  int& param(int& y) { return y; }
  // rules: STN-76
  // expect: not-stone: into *this
  int& global() { return seen; }
  // rules: STN-76
  // expect: not-stone
  int& local() {
    int z = 0;
    int& r = z;
    return r;
  }
  // rules: STN-76
  // expect: not-stone: into *this
  int& mixed(bool c, int& y) { return c ? x : y; }
};

struct Ctx {
  int x;
};

struct Through {
  Ctx& ctx;
  // rules: STN-76, STN-53
  // expect: not-stone: through reference member 'ctx'
  int& get() { return ctx.x; }
};

// --- avvisas hos anroparen ----------------------------------------------------------

// Två anrop som kan ändra s i samma uttryck.
// rules: STN-76, STN-21
// expect: not-stone: side effect inside expression
int two_calls(Slots& s) { return s.counted(0) + s.counted(1); }

// rules: STN-76, STN-21
// expect: not-stone: side effect inside expression
int as_operand(Slots& s) { return s[0] + 1; }

// En referens in i vektorn överlever en omallokering.
// rules: STN-76, STN-62
// expect: not-stone: loans
int dangling(Grow& g) {
  int& r = g[0];
  g.grow();
  return r;
}

// rules: STN-76, STN-62
// expect: not-stone: loans
int dangling_direct(Grow& g) {
  int& r = g[0];
  g.v.push_back(2);
  return r;
}

// Referensen in i en temporär dör vid satsens slut.
// rules: STN-76, STN-31
// expect: not-stone
int temporary_object() {
  int& r = Slots{{1, 2}}[0];
  return r;
}
