// Egna konstruktorer (STN-66). Mätningen mot vanlig C++ visade att konstruktorer
// stoppade 100-150 funktioner per repo (fmt, leveldb, yaml-cpp, tinyxml2).

// --- värdetyper ---

struct Vec {
  float x;
  float y;
  // rules: STN-66
  // expect: stone
  Vec(float a, float b) : x(a), y(b) {}
};

// Anropet till konstruktorn är ett vanligt anrop.
// rules: STN-66, STN-23
// expect: stone
Vec diagonal(float a) { return Vec(a, a); }

// rules: STN-66, STN-23
// expect: stone
float sum(float a) {
  Vec v{a, 2.0F};
  return v.x + v.y;
}

// Standardinitierare räknas.
struct Counter {
  int count = 0;
  int limit;
  // rules: STN-66
  // expect: stone
  explicit Counter(int l) : limit(l) {}
};

// Kroppen är en metod som inte är const, efter att alla fält är initierade.
struct Scaled {
  int value;
  // rules: STN-66
  // expect: stone
  Scaled(int a, int k) : value(a) {
    if (k > 0 && k < 10 && a > -1000 && a < 1000) value = a * k;
  }
};

// Delegerande konstruktor: ett anrop till den andra.
struct Pair {
  int a;
  int b;
  // rules: STN-66
  // expect: stone
  Pair(int x, int y) : a(x), b(y) {}
  // rules: STN-66, STN-23
  // expect: stone
  explicit Pair(int x) : Pair(x, x) {}
};

// Fält av en typ med egen konstruktor: initieraren är ett anrop.
struct Line {
  Vec from;
  Vec to;
  // rules: STN-66, STN-23
  // expect: stone
  Line(float a, float b) : from(a, b), to(b, a) {}
};

// (d) Fält som inte initieras.
struct Partial {
  int x;
  int y;
  // rules: STN-66
  // expect: not-stone: field 'y' of Partial is not initialized by the constructor
  explicit Partial(int a) : x(a) {}
};

// (d) Först tilldelat i kroppen: inte än.
struct Late {
  int x;
  // rules: STN-66
  // expect: not-stone: field 'x' of Late is not initialized by the constructor
  explicit Late(int a) { x = a; }
};

// (c) Initieraren läser ett skalärt fält som redan är initierat: godtas.
struct ReadsEarlier {
  int x;
  int y;
  // rules: STN-66
  // expect: stone
  explicit ReadsEarlier(int a) : x(a), y(x + 1) {}
};

// (c) Även via standardinitieraren, när konstruktorn använder den.
struct DefaultReadsEarlier {
  int x;
  int y = x * 2;
  bool positive = y > 0;
  // rules: STN-66
  // expect: stone
  explicit DefaultReadsEarlier(int a) : x(a) {}
};

// (c) Ett senare fält är inte initierat än.
struct ReadsLater {
  int x;
  int y;
  // rules: STN-66
  // expect: not-stone: initializer of 'x' uses *this
  explicit ReadsLater(int a) : x(y), y(a) {}
};

// (c) Bara skalära fält: en kopia av ett fält av klasstyp kör kod på
// objektet (och kan hålla lån in i det).
struct Inner {
  int v = 0;
};
struct CopiesField {
  Inner i;
  Inner j;
  // rules: STN-66
  // expect: not-stone: initializer of 'j' uses *this
  explicit CopiesField(int a) : i{a}, j(i) {}
};

// (c) Ett anrop av en metod, `this` och en adress till ett fält avvisas.
struct UsesMethod {
  int x;
  int y;
  // rules: STN-7
  // expect: stone
  int get() const { return x; }
  // rules: STN-66
  // expect: not-stone: initializer of 'y' uses *this
  explicit UsesMethod(int a) : x(a), y(get()) {}
};
struct UsesExplicitThis {
  int x;
  int y;
  // rules: STN-66
  // expect: not-stone: initializer of 'y' uses *this
  explicit UsesExplicitThis(int a) : x(a), y(this->x) {}
};
// rules: STN-12
// expect: stone
int read_through(const int& r) { return r; }
struct BindsField {
  int x;
  int y;
  // rules: STN-66
  // expect: not-stone: initializer of 'y' uses *this
  explicit BindsField(int a) : x(a), y(read_through(x)) {}
};

// (c) En implicit konstruktor eller aggregatinitiering: standardinitieraren
// granskas där objektet skapas, och `x` är inte det objektets fält där.
struct ImplicitReadsEarlier {
  int x = 1;
  int y = x + 1;
};
// rules: STN-66
// expect: not-stone: initializer of field 'y' uses *this
int implicit_reads_earlier() {
  ImplicitReadsEarlier p;
  return p.y;
}

// Initieraren gör det som andra uttryck får göra, och inte mer.
int global_count = 0;
struct ReadsGlobal {
  int x;
  // rules: STN-66, STN-22
  // expect: not-stone: uses variable 'global_count' with static storage
  ReadsGlobal() : x(global_count) {}
};

// rules: STN-66, STN-23
// expect: not-stone: calls non-stone 'Partial::Partial'
int use_partial(int a) { return Partial(a).x; }

// (e) Anonym union.
struct Variant {
  union {
    int i;
    float f;
  };
  // rules: STN-66
  // expect: not-stone: member of non-object type Variant
  explicit Variant(int a) : i(a) {}
};

// --- objekttyper (STN-50) ---

struct Port {
  virtual ~Port() = default;
  virtual int get() const = 0;
};

// (b) Portreferens bunden till en referensparameter.
class Service {
 public:
  // rules: STN-66, STN-51
  // expect: stone
  explicit Service(Port& p) : port_(p) {}

 private:
  Port& port_;
  int calls_ = 0;
};

// (b) Referensfält bundet till något annat än en parameter.
struct Holder {
  Port& port_;
};
class Indirect {
 public:
  // rules: STN-66
  // expect: not-stone: reference field 'port_' must be bound to a reference parameter
  explicit Indirect(const Holder& h) : port_(h.port_) {}

 private:
  Port& port_;
};

// Basinitierare: ett anrop till basens konstruktor.
class Base {
 public:
  // rules: STN-66
  // expect: stone
  explicit Base(int n) : n_(n) {}
  // rules: STN-50
  // expect: stone
  virtual ~Base() = default;

 protected:
  int n_;
};
class Derived : public Base {
 public:
  // rules: STN-66, STN-23
  // expect: stone
  Derived() : Base(3) {}
};

// Typer som varken är värdetyper eller objekttyper: som förut.
struct OwnsPointer {
  int* p;
  // rules: STN-66
  // expect: not-stone: member of non-object type OwnsPointer
  explicit OwnsPointer(int* q) : p(q) {}
};

// Implicit standardkonstruktor som kör en egen konstruktor i ett fält
// (hittat i motståndargenomgången). `Outer o{}` var redan rätt;
// `Outer o;` gick genom fältinitierarna och missade Inner::Inner.
int seed = 0;
struct InnerNS {
  int v = 0;
  // rules: STN-66, STN-22
  // expect: not-stone: uses variable 'seed' with static storage
  InnerNS() { v = seed; }
};
struct OuterNS {
  InnerNS i;
  int w = 0;
};

// rules: STN-66, STN-67
// expect: not-stone: calls non-stone 'InnerNS::InnerNS'
int default_init_outer() {
  OuterNS o;
  return o.w;
}

// rules: STN-66, STN-67
// expect: not-stone: calls non-stone 'InnerNS::InnerNS'
int value_init_outer() {
  OuterNS o{};
  return o.w;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"ReadsEarlier::ReadsEarlier", [] { ReadsEarlier r(4); return r.y == 5 ? 0 : 1; }},
    {"DefaultReadsEarlier::DefaultReadsEarlier", [] {
       DefaultReadsEarlier d(3);
       return d.y == 6 && d.positive ? 0 : 1;
     }},
    {"ReadsLater::ReadsLater", [] { ReadsLater r(4); return r.x == 4 ? 1 : 0; }})
#endif
