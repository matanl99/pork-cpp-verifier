// pork-args: --silver
// Silver för klasser med baser (SLV-2: exakt översättning eller ingen). Den
// platta C-structen har basens fält först; en klammerlista med basen som eget
// element översätts inte.
struct Coordinates {
  int x;
  int y;
};

struct Agg : Coordinates {
  int z;
};

// rules: SLV-2
// expect: stone: not silver: cannot translate to C: initializer list of a class with base classes
int aggregate() {
  Agg a{{1, 2}, 3};
  return a.z;
}

// rules: SLV-2
// expect: silver
int take(const Coordinates& c) { return c.x > 0 ? 1 : 0; }

struct Point : Coordinates {
  // rules: SLV-2
  // expect: stone: not silver: cannot translate to C: base initializer
  Point(int a, int b) : Coordinates{a, b} {}
};

struct Hide : Coordinates {
  int x;
};

// rules: SLV-2
// expect: stone: not silver: cannot translate to C: field 'x' of Hide hides a base class field
int hidden(const Hide& h) { return h.x > h.Coordinates::x ? 1 : 0; }
