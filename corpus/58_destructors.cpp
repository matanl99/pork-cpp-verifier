#include <vector>

// Egna destruktorer (STN-68). Varje ställe där en destruktor körs implicit är
// ett anrop till den: en destruktor som inte är Stone gör den kod som
// förstör objektet till not-stone.

int global_count = 0;

// En destruktor som är Stone, och en som inte är det.
struct Calm {
  int v = 0;
  // rules: STN-68
  // expect: stone
  ~Calm() { v = 0; }
};

struct Noisy {
  int v = 0;
  // rules: STN-68, STN-22
  // expect: not-stone: uses variable 'global_count' with static storage
  ~Noisy() { v = global_count; }
};

// En egen destruktor hindrar inte en värdetyp (STN-2).
// rules: STN-68
// expect: stone
int calm_value(Calm c) { return c.v; }

// Lokal variabel.
// rules: STN-68
// expect: stone
int local_calm() {
  Calm c;
  return c.v;
}

// rules: STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int local_noisy() {
  Noisy n;
  return n.v;
}

// Parameter som värde: förstörs i funktionen.
// rules: STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int by_value(Noisy n) { return n.v; }

// Bakom en referens förstörs inget.
// rules: STN-68
// expect: stone
int by_ref(const Noisy& n) { return n.v; }

// Temporär.
// rules: STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int temporary() { return Noisy{}.v; }

// Fält: den implicita destruktorn i Outer kör Noisy::~Noisy.
struct Outer {
  Noisy inner;
  int w = 0;
};
// rules: STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int field_of_local() {
  Outer o;
  return o.w;
}

// En egen destruktor följs av fältens destruktorer.
struct Wrapper {
  Noisy inner;
  // rules: STN-68
  // expect: not-stone: calls non-stone 'Noisy::~Noisy'
  ~Wrapper() {}
};

// Element i en std-behållare förstörs inuti std (STN-67).
// rules: STN-68, STN-67
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
void clear_all(std::vector<Noisy>& v) { v.clear(); }

// rules: STN-68, STN-67
// expect: stone: assuming
void clear_calm(std::vector<Calm>& v) { v.clear(); }

// Range-for med en kopia per varv.
// rules: STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int sum(const std::vector<Noisy>& v) {
  int s = 0;
  for (Noisy n : v)
    if (n.v > 0 && n.v < 1000 && s < 1000) s = s + n.v;
  return s;
}

// Lambda som fångar en kopia: fångsten förstörs med lambdan.
// rules: STN-68
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
int captured(const Noisy& n) {
  auto f = [n]() { return n.v; };
  return f();
}

// Tilldelning ersätter element i en behållare: de gamla förstörs.
struct Bag {
  std::vector<Noisy> items;
};
// rules: STN-68, STN-67
// expect: not-stone: calls non-stone 'Noisy::~Noisy'
void replace(Bag& a, const Bag& b) { a = b; }

// cleanup-attributet kör en funktion vid blockets slut, som en destruktor
// (hittat i motståndargenomgången).
// rules: STN-22
// expect: not-stone
void on_exit(int* p) { global_count = *p; }

// rules: STN-14
// expect: not-stone: local variable 'x' has a cleanup function
int with_cleanup() {
  int x __attribute__((cleanup(on_exit))) = 1;
  return x;
}
