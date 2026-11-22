#include <memory>
#include <utility>
#include <vector>

// std::unique_ptr (STN-69): en betrodd ägande typ. I Stone är en rå pekare
// alltid ett lån (inget `new`), så det som tar över ägandet av en rå pekare
// avvisas. make_unique följs till konstruktorn (STN-67), förstörelsen till
// destruktorn (STN-68), och lån in i det ägda objektet följs av lånemodellen.

struct Vec {
  int x = 0;
  int y = 0;
};

// rules: STN-69
// expect: stone: assuming
std::unique_ptr<Vec> make(int x) { return std::make_unique<Vec>(Vec{x, 0}); }

// rules: STN-69
// expect: stone: assuming
int read(const std::unique_ptr<Vec>& p) { return p ? p->x : 0; }

// rules: STN-69
// expect: stone: assuming
void write(std::unique_ptr<Vec>& p, int v) {
  if (p) p->y = v;
}

// rules: STN-69, STN-57
// expect: stone: assuming
std::unique_ptr<Vec> pass_on(std::unique_ptr<Vec> p) { return p; }

// rules: STN-69
// expect: stone: assuming
void drop(std::unique_ptr<Vec>& p) { p.reset(); }

// rules: STN-69
// expect: stone: assuming
void clear(std::unique_ptr<Vec>& p) { p = nullptr; }

// Ett ägt träd: rekursion via unique_ptr (STN-63).
struct List {
  int v = 0;
  std::unique_ptr<List> next;
};

// rules: STN-69, STN-63
// expect: stone: assuming
int head(const List& l) { return l.v; }

// rules: STN-69, STN-63
// expect: stone: assuming
int second(const List& l) { return l.next ? l.next->v : 0; }

// --- ägande från en rå pekare ---

// rules: STN-69
// expect: not-stone: takes ownership of a raw pointer
int adopt(Vec& v) {
  std::unique_ptr<Vec> u(&v);
  return u->x;
}

// rules: STN-69
// expect: not-stone: takes ownership of a raw pointer
void adopt_reset(std::unique_ptr<Vec>& u, Vec& v) { u.reset(&v); }

// rules: STN-69
// expect: not-stone: releases ownership to a raw pointer
void give_up(std::unique_ptr<Vec>& u) {
  Vec* p = u.release();
  (void)p;
}

// get() är ett lån av det ägda objektet: lånemodellen följer det.
// rules: STN-69, STN-62
// expect: stone: assuming
int via_get(const std::unique_ptr<Vec>& u) {
  const Vec* v = u.get();
  return v != nullptr ? v->x : 0;
}

// rules: STN-69, STN-62
// expect: not-stone: loans
int dangling(std::unique_ptr<Vec>& u) {
  const Vec* v = u.get();
  u.reset();
  return v != nullptr ? v->x : 0;
}

// --- konstruktor och destruktor i den ägda typen ---

int global_seed = 0;
struct Seeded {
  int v;
  // rules: STN-66, STN-22
  // expect: not-stone: uses variable 'global_seed' with static storage
  explicit Seeded(int a) : v(a + global_seed) {}
};

// rules: STN-69, STN-67
// expect: not-stone: calls non-stone 'Seeded::Seeded'
std::unique_ptr<Seeded> make_seeded(int a) { return std::make_unique<Seeded>(a); }

struct Loud {
  int v = 0;
  // rules: STN-68, STN-22
  // expect: not-stone: uses variable 'global_seed' with static storage
  ~Loud() { v = global_seed; }
};

// rules: STN-69, STN-68
// expect: not-stone: calls non-stone 'Loud::~Loud'
void drop_loud(std::unique_ptr<Loud>& p) { p.reset(); }

// --- grunt const (STN-70) ---
// unique_ptr::operator*, operator-> och get() är const men ger en icke-const
// T& / T*. Genom ett const-objekt godtas bara läsning.

// rules: STN-70
// expect: not-stone: writable access through a const std::unique_ptr<Vec>
void sneaky(const std::unique_ptr<Vec>& p) {
  if (p) p->x = 1;
}

// rules: STN-70
// expect: not-stone: writable access through a const std::unique_ptr<Vec>
void sneaky_deref(const std::unique_ptr<Vec>& p) {
  if (p) (*p).x = 1;
}

// rules: STN-70
// expect: not-stone: writable access through a const std::unique_ptr<Vec>
void sneaky_get(const std::unique_ptr<Vec>& p) {
  Vec* v = p.get();
  if (v != nullptr) v->x = 1;
}

// rules: STN-70
// expect: stone
void bump(Vec& v) { v.x = 2; }
// rules: STN-70
// expect: not-stone: writable access through a const std::unique_ptr<Vec>
void sneaky_arg(const std::unique_ptr<Vec>& p) {
  if (p) bump(*p);
}

struct Owner {
  std::unique_ptr<Vec> p;
  // rules: STN-70
  // expect: not-stone: writable access through a const std::unique_ptr<Vec>
  void sneaky() const {
    if (p) p->x = 3;
  }
  // rules: STN-70
  // expect: stone: assuming
  int get() const { return p ? p->x : 0; }
  // rules: STN-70
  // expect: stone: assuming
  void set(int v) {
    if (p) p->x = v;
  }
};

struct Sized {
  int n = 0;
  // rules: STN-70
  // expect: stone
  int size() const { return n; }
};
// rules: STN-70
// expect: stone
int size_of(const Sized& s) { return s.n; }

// Läsande användning genom ett const-objekt.
// rules: STN-70
// expect: stone: assuming
int reads(const std::unique_ptr<Vec>& p, const std::unique_ptr<Sized>& s) {
  if (!p || !s) return 0;
  const Vec& r = *p;
  Vec copy = *p;
  const Vec* g = p.get();
  int a = (*p).x;
  int b = s->size();
  int c = size_of(*s);
  return (r.x == copy.x && g != nullptr && p->y == a) ? b + c : 0;
}
