// Värdetyper med icke-virtuella baser (STN-2): basens fält är en del av
// objektet. En bas måste initieras som ett fält (STN-66), vägar genom basen
// är vägar in i objektet, och ett dolt namn är ett annat fält.
struct Coordinates {
  int x;
  int y;
};

struct Point : Coordinates {
  // rules: STN-2, STN-66
  // expect: stone
  Point(int a, int b) : Coordinates{a, b} {}
};

// rules: STN-2
// expect: stone
int use_value() {
  Point p{4, 5};
  return 10 * p.x + p.y;
}

struct Agg : Coordinates {
  int z;
};

// rules: STN-2
// expect: stone
int aggregate() {
  Agg a{{1, 2}, 3};
  return a.x + a.z;
}

// Saknade element värdeinitieras.
// rules: STN-2
// expect: stone
int aggregate_partial() {
  Agg a{{1}, 3};
  return a.y + a.z;
}

struct Tag {};

struct Tagged : Tag {
  int v;
  // En tom bas har inget att initiera.
  // rules: STN-2, STN-66
  // expect: stone
  Tagged(int a) : v(a) {}
};

struct Valued : Coordinates {
  // Coordinates() värdeinitierar (nollor).
  // rules: STN-2, STN-66
  // expect: stone
  Valued() : Coordinates() {}
};

// rules: STN-2
// expect: stone
int sliced(const Point& p) {
  Coordinates c = p;
  return c.x;
}

// rules: STN-2
// expect: stone
void set(Coordinates& c, const int& v) {
  c.x = 0;
  c.y = v;
}

// rules: STN-2
// expect: stone
int write_through_base(Point& p) {
  p.x = 3;
  Coordinates& c = p;
  c.y = 4;
  return p.y;
}

struct Hide : Coordinates {
  int x;
};

// rules: STN-2
// expect: stone
void two(int& a, int& b) {
  a = 1;
  b = 2;
}

// Hide::x och Coordinates::x är olika fält.
// rules: STN-2, STN-62
// expect: stone
int hidden_distinct() {
  Hide h{{1, 2}, 3};
  two(h.x, h.Coordinates::x);
  return h.x;
}

struct Sum : Coordinates {
  // rules: STN-2, STN-7
  // expect: stone
  int sum() const { return x + y; }
};

// rules: STN-2
// expect: stone
int call_base_fields(const Sum& s) { return s.sum(); }

// --- avvisas -----------------------------------------------------------------

struct Uninit : Coordinates {
  int z;
  // Fanns före: basens triviala standardkonstruktor lämnar x och y oinitierade.
  // rules: STN-2, STN-66
  // expect: not-stone: base Coordinates of Uninit is not initialized
  Uninit(int c) : z(c) {}
};

// rules: STN-2, STN-15
// expect: not-stone: is not initialized
int aggregate_default() {
  Agg a;
  return a.z;
}

// Basen och fältet överlappar: p som Coordinates& är hela p.
// rules: STN-2, STN-62
// expect: not-stone: may be the same object
void alias(Point& p) { set(p, p.x); }

// rules: STN-2, STN-62
// expect: not-stone: may be the same object
int hidden_same() {
  Hide h{{1, 2}, 3};
  two(h.x, h.x);
  return h.x;
}

int g = 0;

struct Logging {
  int v;
  // rules: STN-2, STN-68
  // expect: not-stone: static storage
  ~Logging() { g = v; }
};

struct FromLogging : Logging {
  int w;
};

// Basens destruktor körs när d tar slut.
// rules: STN-2, STN-23, STN-68
// expect: not-stone: calls non-stone 'Logging::~Logging'
int base_destructor() {
  FromLogging d{{1}, 2};
  return d.w;
}

struct Poly {
  virtual ~Poly() = default;
  int x;
};

struct FromPoly : Poly {
  int y;
};

// rules: STN-2, STN-50
// expect: not-stone: polymorphic
int polymorphic(FromPoly f) { return f.y; }

struct Shared : virtual Coordinates {
  int z;
};

// rules: STN-2
// expect: not-stone: virtual base
int virtual_base(const Shared& s) { return s.z; }
