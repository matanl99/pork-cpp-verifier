// pork-args: --silver
// SLV-40: i C är varje kopia bitvis, så typer med egna kopierings- eller
// flyttfunktioner översätts inte än, inte heller som fält.
struct Copied {
  int v = 0;
  // rules: SLV-40
  // expect: stone: not silver: cannot translate to C: type Copied with a user-provided copy or move
  Copied() = default;
  // rules: SLV-40
  // expect: stone: not silver: cannot translate to C: type Copied with a user-provided copy or move
  Copied(const Copied& o) : v(o.v + 1) {}
};
struct Outer {
  Copied c;
};

// Utan SLV-40 blev den här silver: C kopierar Outer bitvis, C++ kör
// Copied(const Copied&), som kan spilla över.
// rules: SLV-40
// expect: stone: not silver: cannot translate to C: type Copied with a user-provided copy or move
int enclosing_copy(int x) {
  Outer o{};
  o.c.v = x;
  Outer p = o;
  return p.c.v - o.c.v;
}

struct Plain {
  int n = 0;
  // rules: SLV-5
  // expect: stone: not silver: arithmetic overflow on signed +
  Plain& operator+=(const Plain& o) {
    n = n + o.n;
    return *this;
  }
};

// rules: SLV-2
// expect: stone: not silver: cannot translate to C: local reference to Plain::operator+=
int bind_result() {
  Plain a{1};
  Plain b{2};
  Plain& r = (a += b);
  return r.n;
}
