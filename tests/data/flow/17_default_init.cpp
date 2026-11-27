// Standardinitierare under en initieringslista (STN-104): implicit this är
// objektet som listan initierar, inte den analyserade metodens. Läsningen av
// ett tidigare fält kommer ur listans element, som redan finns med i
// aggregatets beroenden.
struct P {
  int a = 1;
  int b = a + 1;
};

struct Outer {
  P p;
  int k;
};

struct C {
  int a;

  // flow: return <- x
  int make_from(int x) const {
    P p{x};
    return p.b;
  }

  // flow: this.a <- x (always written)
  void store(int x) {
    P p{x};
    a = p.a;
  }

  // flow: return <- x
  int nested(int x) const {
    Outer o{{x}, 3};
    return o.p.b;
  }

  // Metodens eget fält läses fortfarande när det står i listan.
  // flow: return <- this.a
  int from_own_field() const {
    P p{a};
    return p.b;
  }
};

// flow: return <- nothing
int defaults_only() {
  P p{};
  return p.b;
}
