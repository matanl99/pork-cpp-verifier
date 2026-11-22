// Värdetyper (structar av Stone-typer), const&-parametrar och const-metoder.

int g = 0;

struct Vec2 { int x; int y; };
struct Rect { Vec2 min; Vec2 max; };
struct WithDefault { int a = 1; int b = 2; };
struct WithGlobalDefault { int a = g; };

struct Holder { int* p; };
struct Nested { Holder h; };
struct Shape { virtual ~Shape() = default; int n; };
struct Owner { Owner(const Owner&); int n; };
struct Base { int a; };
struct Derived : Base { int b; };
union U { int i; float f; };
struct WithMutable { mutable int cache; int v; };
struct Arr { int v[3]; };

// --- Godkända ---

// rules: STN-2, STN-18
// expect: stone
Vec2 make(int x, int y) { return Vec2{x, y}; }

// rules: STN-2, STN-15
// expect: stone
Vec2 make_named(int x, int y) { Vec2 v{x, y}; return v; }

// rules: STN-2
// expect: stone
int dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

// rules: STN-2, STN-12
// expect: stone
int area(const Rect& r) { return (r.max.x - r.min.x) * (r.max.y - r.min.y); }

// rules: STN-2, STN-12
// expect: stone
Vec2 add(const Vec2& a, const Vec2& b) { Vec2 r = a; r.x += b.x; r.y += b.y; return r; }

// rules: STN-2, STN-20
// expect: stone
Vec2 assign(Vec2 a, Vec2 b) { a = b; return a; }

// rules: STN-18, STN-28
// expect: stone
int uses_temporary(int x) { return area(Rect{Vec2{0, 0}, Vec2{x, x}}); }

// rules: STN-15
// expect: stone
int zero_init() { Vec2 v{}; return v.x + v.y; }

// rules: STN-15
// expect: stone
int with_defaults() { WithDefault w{}; return w.a + w.b; }

// rules: STN-12
// expect: stone
int scalar_const_ref(const int& a) { return a + 1; }

// --- Underkända: hur värdet används ---

// rules: STN-15, STN-22
// expect: not-stone: uses variable 'g'
int with_global_default() { WithGlobalDefault w{}; return w.a; }

// rules: STN-12
// expect: stone
void out_param(Vec2& r) { r.x = 0; }

// rules: STN-10
// expect: stone
const Vec2& returns_ref(const Vec2& v) { return v; }

// rules: STN-31
// expect: stone
int local_ref(Vec2 v) { const Vec2& r = v; return r.x; }

// rules: STN-12
// expect: not-stone: parameter 'v' has type Vec2 &&
int rvalue_ref(Vec2&& v) { return v.x; }

// --- Underkända: typen är ingen värdetyp ---

// rules: STN-2
// expect: not-stone: (field 'p' of Holder has type int *)
int holder(Holder h) { return h.p != nullptr; }

// rules: STN-2
// expect: not-stone: (field 'h' of Nested has type Holder (field 'p' of Holder has type int *))
int nested(const Nested& n) { return n.h.p != nullptr; }

// En polymorf klass är ingen värdetyp, men en objekttyp: bakom en referens går bra.
// rules: STN-50
// expect: stone
int shape(const Shape& s) { return s.n; }

// Som värde skulle den skäras av (slicing).
// rules: STN-2, STN-50
// expect: not-stone: (Shape is polymorphic)
int shape_by_value(Shape s) { return s.n; }

// En egen kopieringskonstruktor gör inte typen till en icke-värdetyp
// (STN-72); här kopieras den inte ens.
// rules: STN-2, STN-72
// expect: stone
int owner(const Owner& o) { return o.n; }

// Kopian anropar konstruktorn, som saknar definition.
// rules: STN-72, STN-23
// expect: not-stone: with no definition
int owner_copy(const Owner& o) { Owner c = o; return c.n; }

// rules: STN-2
// expect: stone
int derived(Derived d) { return d.b; }

// rules: STN-2
// expect: not-stone: (U is a union)
int uni(U u) { return u.i; }

// rules: STN-2
// expect: not-stone: (field 'cache' of WithMutable is mutable)
int mut(WithMutable w) { return w.v; }

// rules: STN-2, STN-3, STN-75
// expect: stone
int arr(Arr a) { return a.v[0]; }

// --- Medlemsfunktioner ---

struct Account {
  int balance;
  int limit;

  // rules: STN-7
  // expect: stone
  int available() const { return balance + limit; }

  // rules: STN-7
  // expect: stone
  bool can_withdraw(int amount) const { return amount <= available(); }

  // rules: STN-7
  // expect: stone
  bool same_as(const Account& other) const {
    return balance == other.balance && this->limit == other.limit;
  }

  // rules: STN-7
  // expect: stone
  static int fee() { return 5; }

  // rules: STN-7
  // expect: stone
  void deposit(int amount) { balance += amount; }

  // rules: STN-18
  // expect: not-stone: unsupported expression: CXXThisExpr
  bool is_self(const Account& other) const { return this == &other; }
};

struct Widget {
  int* data;
  // rules: STN-7
  // expect: not-stone: member of non-object type Widget (field 'data' of Widget has type int *)
  int first() const { return 0; }
};

// rules: STN-7, STN-23
// expect: stone
int total(const Account& a, const Account& b) { return a.available() + b.available() - Account::fee(); }

// rules: STN-7, STN-21
// expect: stone
int deposit_then(Account a) { a.deposit(1); return a.balance; }

// --- Användardefinierade operatorer = vanliga anrop ---

// rules: STN-23
// expect: stone
Vec2 operator-(Vec2 a, Vec2 b) { return Vec2{a.x - b.x, a.y - b.y}; }

// rules: STN-23
// expect: stone
int manhattan(Vec2 a, Vec2 b) { Vec2 d = a - b; return (d.x < 0 ? -d.x : d.x) + (d.y < 0 ? -d.y : d.y); }

struct Money {
  int cents;
  // rules: STN-7
  // expect: stone
  bool operator==(const Money& o) const { return cents == o.cents; }
  // rules: STN-73
  // expect: stone
  Money& operator+=(Money o) { cents += o.cents; return *this; }
};

// rules: STN-23
// expect: stone
bool same_money(Money a, Money b) { return a == b; }

// rules: STN-23, STN-73
// expect: stone
int add_money(Money a, Money b) { a += b; return a.cents; }

// a != b utan egen operator!= (C++20): clang skriver om till !(a == b).
// rules: STN-18, STN-23
// expect: stone
bool money_differs(Money a, Money b) { return a != b; }
