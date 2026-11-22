// Konverteringsfunktioner (STN-8): en metod som anropas där konverteringen
// står, explicit (static_cast) eller implicit. Anropet granskas som vilket
// metodanrop som helst: sidoeffekter, referensreturer och lån.
int counter = 0;

struct Value {
  int number;
  // rules: STN-8
  // expect: stone
  explicit operator int() const { return number; }
};

// rules: STN-8
// expect: stone
int explicit_use(const Value& v) { return static_cast<int>(v) + 1; }

struct Flag {
  bool on;
  // rules: STN-8
  // expect: stone
  explicit operator bool() const { return on; }
};

// rules: STN-8
// expect: stone
int contextual(const Flag& f) {
  if (f) return 1;
  return 0;
}

struct Implicit {
  int number;
  // rules: STN-8
  // expect: stone
  operator int() const { return number; }
};

// rules: STN-8
// expect: stone
int implicit_use(const Implicit& v) { return v + 1; }

// --- avvisas -------------------------------------------------------------

struct Counting {
  int number;
  int reads;
  // Icke-const: skriver *this.
  // rules: STN-8
  // expect: stone
  operator int() {
    reads = reads + 1;
    return number;
  }
};

// Den implicita konverteringen är ett anrop som ändrar v inuti ett uttryck.
// rules: STN-8, STN-21
// expect: not-stone: side effect inside expression
int hidden_write(Counting& v) { return v + 1; }

// rules: STN-8
// expect: stone
int write_statement(Counting& v) {
  int x = v;
  return x;
}

struct Global {
  // rules: STN-8
  // expect: not-stone: static storage
  explicit operator int() const { return counter; }
};

// rules: STN-8
// expect: not-stone: calls non-stone
int global_use(const Global& g) { return static_cast<int>(g); }

struct Holder {
  int number;
  // rules: STN-8, STN-10
  // expect: stone
  operator const int&() const { return number; }
};

// Referensen från konverteringen lånar h.number; att skriva fältet dödar
// inte lånet (STN-62), r läser det nya värdet.
// rules: STN-8, STN-62
// expect: stone
int modify_while_bound(Holder& h) {
  const int& r = h;
  h.number = 5;
  return r;
}

struct Ctx {
  int number;
};

struct Through {
  Ctx& ctx;
  // rules: STN-8, STN-76
  // expect: not-stone: reference member
  operator int&() { return ctx.number; }
};

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"modify_while_bound", [] { Holder h{1}; return modify_while_bound(h) == 5 ? 0 : 1; }})
#endif
