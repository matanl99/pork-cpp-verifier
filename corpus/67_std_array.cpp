// Klammerinitiering av std::array (STN-75): std::array är ett aggregat med
// ett arrayfält, så {2, 3} innehåller en initieringslista av arraytyp.
// Fallen kommer från gruppexemplen 024 och 034.
#include <array>

int seen = 0;

struct Value {
  int n;
};

struct Counted {
  int n;
  // rules: STN-75
  // expect: not-stone: static storage
  Counted() : n(0) { ++seen; }
  // rules: STN-75
  // expect: not-stone: static storage
  explicit Counted(int x) : n(x) { ++seen; }
};

struct Plain {
  int n;
  // rules: STN-75
  // expect: stone
  Plain() : n(0) {}
};

struct Copied {
  int n;
  // rules: STN-75
  // expect: stone
  Copied(int x) : n(x) {}
  // rules: STN-75
  // expect: not-stone: static storage
  Copied(const Copied& o) : n(o.n) { ++seen; }
};

// Bara standardkonstruktorn är förorenad: fyllnaden måste hittas.
struct DefaultBad {
  int n;
  // rules: STN-75
  // expect: not-stone: static storage
  DefaultBad() : n(0) { ++seen; }
  // rules: STN-75
  // expect: stone
  explicit DefaultBad(int x) : n(x) {}
};

struct Outer {
  DefaultBad d;
};

struct Slots {
  std::array<int, 2> data;
};

struct Defaulted {
  std::array<int, 3> data{1, 2, 3};
};

// --- godtas -----------------------------------------------------------------

// rules: STN-75
// expect: stone: assuming
int elided() {
  std::array<int, 2> a{2, 3};
  return a[0] + a[1];
}

// rules: STN-75
// expect: stone: assuming
int double_braces() {
  std::array<int, 2> a{{2, 3}};
  return a[1];
}

// rules: STN-75
// expect: stone: assuming
int value_init() {
  std::array<int, 3> a{};
  return a[2];
}

// Resten värdeinitieras (array_filler).
// rules: STN-75
// expect: stone: assuming
int partial() {
  std::array<int, 3> a{1};
  return a[0] + a[2];
}

// rules: STN-75
// expect: stone: assuming
int structs() {
  std::array<Value, 3> v{{{9}, {2}, {4}}};
  return v[0].n;
}

// rules: STN-75
// expect: stone: assuming
int nested() {
  std::array<std::array<int, 2>, 2> g{{{1, 2}, {3, 4}}};
  return g[1][0];
}

// rules: STN-75
// expect: stone: assuming
int in_field(int x) {
  Slots s{{x, 3}};
  s.data[1] = 9;
  return s.data[0] + s.data[1];
}

// rules: STN-75
// expect: stone: assuming
int member_default() {
  Defaulted d;
  return d.data[2];
}

// Fyllnaden kör en egen standardkonstruktor som är Stone.
// rules: STN-75, STN-66
// expect: stone: assuming
int filler_plain() {
  std::array<Plain, 3> a{};
  return a[2].n;
}

// rules: STN-75
// expect: stone: assuming
int from_params(int x, int y) {
  std::array<int, 2> a{x, y};
  return a[0] - a[1];
}

// --- avvisas: egen kod i elementen ------------------------------------------------

// rules: STN-75, STN-66
// expect: not-stone: Counted::Counted
int element_ctor() {
  std::array<Counted, 2> a{Counted{1}, Counted{2}};
  return a[0].n;
}

// Elementen som inte ges skapas med DefaultBad(): fyllnaden granskas.
// rules: STN-75, STN-66
// expect: not-stone: DefaultBad::DefaultBad
int filler_ctor() {
  std::array<DefaultBad, 3> a{DefaultBad{1}};
  return a[0].n;
}

// rules: STN-75, STN-66
// expect: not-stone: DefaultBad::DefaultBad
int filler_value_init() {
  std::array<DefaultBad, 2> a{};
  return a[0].n;
}

// rules: STN-75, STN-66
// expect: not-stone: DefaultBad::DefaultBad
int filler_nested() {
  std::array<Outer, 2> a{};
  return a[0].d.n;
}

// rules: STN-75, STN-66
// expect: not-stone: DefaultBad::DefaultBad
int filler_nested_partial() {
  std::array<Outer, 2> a{Outer{DefaultBad{1}}};
  return a[0].d.n;
}

// rules: STN-75, STN-66
// expect: stone: assuming
int all_given() {
  std::array<DefaultBad, 2> a{DefaultBad{1}, DefaultBad{2}};
  return a[1].n;
}

// Kopian av hela arrayen kopierar varje element med Copied(const Copied&).
// rules: STN-75, STN-72
// expect: not-stone: Copied::Copied
int copy_elements() {
  std::array<Copied, 2> a{Copied{1}, Copied{2}};
  std::array<Copied, 2> b = a;
  return b[0].n;
}

// --- avvisas: övrigt ------------------------------------------------------------

// rules: STN-75
// expect: stone
int next(int& x) {
  x = x + 1;
  return x;
}

// rules: STN-75, STN-21
// expect: not-stone: side effect inside expression
int element_effect(int x) {
  std::array<int, 2> a{next(x), 2};
  return a[0];
}

// rules: STN-75
// expect: stone
int c_array() {
  int a[2] = {1, 2};
  return a[0];
}

struct WithArray {
  int a[2];
};

// rules: STN-75, STN-3
// expect: stone
int c_array_field() {
  WithArray w{{1, 2}};
  return w.a[0];
}

// En betrodd egen mall med ett arrayfält av pekare: typen litas på, men
// elementen får inte vara lån.
template <class T>
struct Holder {
  T* slots[2];
};

// rules: STN-75, STN-4
// expect: not-stone: array field of Holder<int> with elements of type int *
int trusted_pointer_array(int x) {
  Holder<int> h{{&x, &x}};
  return x;
}

// rules: STN-75, STN-4
// expect: not-stone
int pointers(int x) {
  std::array<int*, 1> a{&x};
  return *a[0];
}

struct FixedBuffer {
  char data[8]{};
  // rules: STN-66, STN-75
  // expect: stone
  FixedBuffer() = default;
  // rules: STN-66, STN-75
  // expect: stone
  explicit FixedBuffer(char c) : data{c} {}
  // rules: STN-18, STN-75
  // expect: stone
  char read(int i) const { return data[i]; }
  // rules: STN-18, STN-75
  // expect: stone
  void write(int i, char c) { data[i] = c; }
};

struct PartialBuffer {
  char data[8];
  // rules: STN-66
  // expect: not-stone: is not initialized by the constructor
  PartialBuffer() { data[0] = '\0'; }
};

struct CharArray { char data[8]; };

// rules: STN-15, STN-75
// expect: stone
char default_buffer() { FixedBuffer b; return b.read(0); }

// rules: STN-75
// expect: stone
char literal_buffer() { CharArray b{"ok"}; return b.data[0]; }

// rules: STN-15
// expect: not-stone: is not initialized
char uninitialized_buffer() { CharArray b; return b.data[0]; }

// rules: STN-19, STN-75
// expect: not-stone: unsupported cast: ArrayToPointerDecay
const char* array_decay(const FixedBuffer& b) { return b.data; }

// rules: STN-12, STN-20
// expect: stone
void change_chars(char& a, const char& b) { a = b; }

// rules: STN-62
// expect: not-stone: may be the same object
void overlap_chars(FixedBuffer& b, int i, int j) { change_chars(b.data[i], b.data[j]); }

// Olika konstanta index är skilda element (STN-86).
// rules: STN-75, STN-86
// expect: stone
void two_cells(FixedBuffer& b) { change_chars(b.data[0], b.data[7]); }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    // Initiering (G4, MSan): alla element är satta efter konstruktionen.
    {"FixedBuffer::FixedBuffer@250", [] { FixedBuffer b; int s = 0; for (char c : b.data) s += c; return s; }},
    {"FixedBuffer::FixedBuffer@253", [] { FixedBuffer b('a'); return b.data[0] == 'a' && b.data[7] == 0 ? 0 : 1; }},
    {"literal_buffer", [] { return literal_buffer() == 'o' ? 0 : 1; }},
    {"default_buffer", [] { return default_buffer() == 0 ? 0 : 1; }},
    // Skrivning genom indexering ändrar bara elementet.
    {"FixedBuffer::write", [] { FixedBuffer b; b.write(7, 'z'); return b.read(7) == 'z' && b.data[6] == 0 ? 0 : 1; }},
    {"FixedBuffer::read", [] { FixedBuffer b('k'); return b.read(0) == 'k' ? 0 : 1; }},
    {"two_cells", [] { FixedBuffer b; b.data[7] = 'w'; two_cells(b); return b.data[0] == 'w' ? 0 : 1; }},
    {"overlap_chars", [] { FixedBuffer b('m'); overlap_chars(b, 0, 0); return b.data[0] == 'm' ? 0 : 1; }},
    {"c_array_field", [] { return c_array_field() == 1 ? 0 : 1; }})
#endif
