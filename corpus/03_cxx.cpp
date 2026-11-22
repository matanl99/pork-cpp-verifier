// C++-specifikt som (än så länge) ligger utanför Stone.

#include <cstdint>
#include <optional>

struct Point { int x; int y; };

enum class Color { red, green };

struct Counter {
  int n = 0;
  // rules: STN-7
  // expect: stone
  int get() const { return n; }
};

// rules: STN-1, STN-19
// expect: stone
std::int32_t fixed_width(std::int32_t a, std::uint8_t b) { return a + b; }

// rules: STN-1
// expect: stone
int from_enum(Color c) { return c == Color::red ? 1 : 2; }

// rules: STN-2
// expect: stone
int by_value_struct(Point p) { return p.x; }

// Bara <stdexcept>-typer med en strängliteral (STN-83); ett int bär inga lån.
// rules: STN-83
// expect: not-stone: throws something other than a <stdexcept> type with a string literal
// loans: ok
int throws(int a) { if (a < 0) throw 1; return a; }

// rules: STN-18
// expect: not-stone: unsupported expression: CXXDeleteExpr
// loans: rejected: cannot follow the loans of
void allocates() { delete new int(1); }

// rules: STN-19
// expect: not-stone: unsupported expression: CXXReinterpretCastExpr
// loans: rejected: cannot follow the loans of
long reinterpret(long a) { return reinterpret_cast<long>(reinterpret_cast<void*>(a)); }

// sizeof/alignof är konstanter (operanden utvärderas inte).
// rules: STN-18
// expect: stone
unsigned long uses_sizeof(int a) { return sizeof(a) + sizeof(long) + alignof(Point); }

// Lambdor: se 16_variant.cpp. (Deras operator() rapporteras inte separat.)
// rules: STN-40
// expect: stone
int uses_lambda(int a) {
  auto f = [] { return 1; };
  return a + f();
}

// Utan trust-fil är std::optional ingen värdetyp (den lagrar värdet i en union).
// rules: STN-2, STN-4
// expect: not-stone: is a union
int untrusted_optional(std::optional<int> o) { return 0; }

// rules: STN-5
// expect: not-stone: variadic function
int variadic(int n, ...) { return n; }

// Destruktorer och konverteringsfunktioner: inte än. Konstruktorer: STN-66,
// se corpus/55_constructors.cpp.
struct WithCtor {
  int x;
  // rules: STN-66
  // expect: stone
  WithCtor(int a) : x(a) {}
};
// rules: STN-66, STN-23
// expect: stone
int use_ctor(int a) { WithCtor c(a); return c.x; }

struct WithDtor {
  int x;
  // rules: STN-68
  // expect: stone
  ~WithDtor() {}
};

struct WithConv {
  int x;
  // rules: STN-8
  // expect: stone
  operator int() const { return x; }
};
