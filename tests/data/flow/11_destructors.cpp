// Förstörelse av lokaler och temporärer. En implicit eller = default-
// destruktor förstör bara baser och fält, och följs ner i dem: en struct
// med std::vector är inte egen kod. En destruktor med egen kropp kan göra
// vad som helst som flödet inte ser: not computed.
#include <optional>
#include <variant>
#include <vector>

struct Bag {
  std::vector<int> items;
  std::optional<int> best;
};
// flow: return <- x
int implicit_dtor(int x) {
  Bag b;
  b.items.push_back(x);
  return x;
}

struct Defaulted {
  std::vector<int> items;
  // flow: not computed: destructor
  ~Defaulted() = default;
};
// flow: return <- x
int defaulted_dtor(int x) {
  Defaulted d;
  return x;
}

struct OutOfLine {
  std::vector<int> items;
  ~OutOfLine();
};
// flow: not computed: destructor
OutOfLine::~OutOfLine() = default;
// flow: return <- x
int out_of_line_default(int x) {
  OutOfLine o;
  return x;
}

struct Guard {
  int v = 0;
  // flow: not computed: destructor
  ~Guard() {}
};
// flow: not computed: destructor with user code
int user_dtor(int x) {
  Guard g{x};
  return g.v;
}

// Egen kod längre ner: i ett fält eller i en bas. (En array av structar
// godtar inte Stone.)
struct HoldsGuard {
  std::vector<int> items;
  Guard g;
};
// flow: not computed: destructor with user code
int user_dtor_in_field(int x) {
  HoldsGuard h;
  return x;
}
struct DerivedFromGuard : Guard {
  int w = 0;
};
// flow: not computed: destructor with user code
int user_dtor_in_base(int x) {
  DerivedFromGuard d;
  return x;
}
// En temporär med egen destruktor.
// flow: not computed: destructor with user code
int user_dtor_temporary(int x) { return Guard{x}.v; }

// En mall i standardbiblioteket förstör sina typargument: egen kod i T syns
// genom std::optional<T>, std::vector<T> och std::variant<..., T>, också i ett
// fält i en struct med implicit destruktor.
struct Wrapped {
  std::optional<Guard> g;
};
// flow: not computed: destructor with user code
int user_dtor_in_optional_field(int x) {
  Wrapped w;
  return x;
}
// flow: not computed: destructor with user code
int user_dtor_in_optional(int x) {
  std::optional<Guard> g;
  return x;
}
// flow: not computed: destructor with user code
int user_dtor_in_vector(int x) {
  std::vector<Guard> gs;
  return x;
}
// flow: not computed: destructor with user code
int user_dtor_in_nested(int x) {
  std::vector<std::optional<Guard>> gs;
  return x;
}
// flow: not computed: destructor with user code
int user_dtor_in_variant(int x) {
  std::variant<int, Guard> v;
  return x;
}
// Utan egen kod i typargumenten: flödet räknas.
// flow: return <- x
int optional_of_plain(int x) {
  std::optional<Bag> b;
  return x;
}
