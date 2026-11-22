// pork-args: --silver
// SLV-41: en metod som ger int& in i *this översätts inte (anroparen skriver
// genom resultatet).
struct Pair {
  int a;
  int b;
  // rules: SLV-41, STN-76
  // expect: stone: not silver: cannot translate to C: return type int & (a non-const reference, not translated yet)
  int& first() { return a; }
};

// rules: SLV-41, STN-76
// expect: stone: not silver
void set_first(Pair& p) { p.first() = 3; }
