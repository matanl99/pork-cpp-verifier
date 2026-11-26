// Tomma std::unique_ptr (STN-89): *p och p-> kräver att p inte är tom
// ([unique.ptr.single.observers]). Parametrar och *this antas inte tomma; det
// en funktion förlitar sig på blir ett krav som anroparen kontrolleras mot (som
// aliasregeln). reset(), = nullptr, flytt, standardkonstruktion och nullptr gör
// en väg "kan vara tom"; if (p), !p, p != nullptr och tilldelning från
// make_unique gör den icke-tom igen. Ett element i en behållare (v[*]) kan inte
// göras icke-tomt av en kontroll av ett annat element, bara genom en referens.
#include <memory>
#include <utility>
#include <vector>

struct R {
  int value;
};

struct Owner {
  std::unique_ptr<R> r;
  bool on;
  // rules: STN-89
  // expect: stone: assuming
  int read() const { return r->value; }
  // rules: STN-89
  // expect: stone: assuming
  void drop() { r.reset(); }
};

// rules: STN-89
// expect: stone: assuming
int deref(const Owner& o) { return o.r->value; }

// rules: STN-89
// expect: stone: assuming
int checked(const Owner& o) { return o.r ? o.r->value : 0; }

// rules: STN-89
// expect: stone: assuming
void close(Owner& o) { o.r.reset(); }

struct Pair {
  std::unique_ptr<R> a;
  std::unique_ptr<R> b;
  // rules: STN-89
  // expect: stone: assuming
  Pair() : a(std::make_unique<R>(R{1})) {}
};

// rules: STN-89
// expect: stone: assuming
std::unique_ptr<R> maybe_make(bool c) {
  if (c) return std::make_unique<R>(R{1});
  return nullptr;
}

// rules: STN-89
// expect: stone: assuming
void add_empty(std::vector<std::unique_ptr<R>>& v) { v.emplace_back(); }

struct Ctx {
  Owner& o;
  // rules: STN-89
  // expect: stone: assuming
  void kill() { o.r.reset(); }
  // rules: STN-89
  // expect: stone: assuming
  int read() const { return o.r->value; }
};

// --- godtas -----------------------------------------------------------------

// rules: STN-89
// expect: stone: assuming
int reset_then_check(Owner& o) {
  o.r.reset();
  return o.r ? o.r->value : 0;
}

// rules: STN-89
// expect: stone: assuming
int close_then_reassign(Owner& o) {
  close(o);
  o.r = std::make_unique<R>(R{2});
  return deref(o);
}

// rules: STN-89
// expect: stone: assuming
int early_return(Owner& o, bool c) {
  if (c) close(o);
  if (!o.r) return 0;
  return deref(o);
}

// rules: STN-89
// expect: stone: assuming
int both(const std::unique_ptr<R>& a, const std::unique_ptr<R>& b) {
  if (!a || !b) return 0;
  return a->value + b->value;
}

// rules: STN-89
// expect: stone: assuming
int constructed_half(int k) {
  Pair p;
  return p.a->value + k;
}

// rules: STN-89
// expect: stone: assuming
int pushed() {
  std::vector<std::unique_ptr<R>> v;
  v.push_back(std::make_unique<R>(R{4}));
  return v[0]->value;
}

// rules: STN-89
// expect: stone: assuming
int get_checked(Owner& o) {
  o.r.reset();
  R* b = o.r.get();
  return b != nullptr ? b->value : 0;
}

// Genom en referens till ett element: kontrollen gäller just det elementet.
// rules: STN-89
// expect: stone: assuming
int element_by_reference(std::vector<std::unique_ptr<R>>& v) {
  v[0].reset();
  auto& p = v[1];
  if (!p) return 0;
  return p->value;
}

// rules: STN-89
// expect: stone: assuming
int swap_full(Owner& o) {
  auto e = std::make_unique<R>(R{5});
  o.r.swap(e);
  return o.r->value;
}

// rules: STN-89
// expect: stone: assuming
int temporary_full() { return deref(Owner{std::make_unique<R>(R{1}), true}); }

// --- underkänns -------------------------------------------------------------

// rules: STN-89
// expect: not-stone: loans: dereferences 'o.r', which may be empty
int reset_then_read(Owner& o) {
  o.r.reset();
  return o.r->value;
}

// rules: STN-89
// expect: not-stone: loans: passes 'o.r', which may be empty, to 'deref', which assumes it is not
int close_then_read(Owner& o) {
  close(o);
  return deref(o);
}

// rules: STN-89
// expect: not-stone: loans: passes 'o.r', which may be empty, to 'Owner::read', which assumes it is not
int drop_then_read(Owner& o) {
  o.drop();
  return o.read();
}

// rules: STN-89
// expect: not-stone: loans: passes 'o.r', which may be empty, to 'deref', which assumes it is not
int maybe_closed(Owner& o, bool c) {
  if (c) close(o);
  return deref(o);
}

// rules: STN-89
// expect: not-stone: loans: passes 'o.r', which may be empty, to 'deref', which assumes it is not
int default_member() {
  Owner o{};
  return deref(o);
}

// rules: STN-89
// expect: not-stone: loans: passes 'o.r', which may be empty, to 'Owner::read', which assumes it is not
int never_set() {
  Owner o{nullptr, true};
  return o.read();
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'p', which may be empty
int default_local() {
  std::unique_ptr<R> p;
  return p->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'o.r', which may be empty
int assign_null(Owner& o) {
  o.r = nullptr;
  return o.r->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'p', which may be empty
int maybe_result() {
  auto p = maybe_make(true);
  return p->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'b', which may be null
int get_after_reset(Owner& o) {
  o.r.reset();
  R* b = o.r.get();
  return b->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'v[*]', which may be empty
int emplaced_empty() {
  std::vector<std::unique_ptr<R>> v;
  v.emplace_back();
  return v[0]->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'v[*]', which may be empty
int after_add(std::vector<std::unique_ptr<R>>& v) {
  add_empty(v);
  return v.back()->value;
}

// Ett annat element: v[0] och v[1] är samma plats v[*].
// rules: STN-89
// expect: not-stone: loans: dereferences 'v[*]', which may be empty
int other_element(std::vector<std::unique_ptr<R>>& v) {
  v[0].reset();
  return v[1]->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'p.b', which may be empty
int constructed_other_half() {
  Pair p;
  return p.b->value;
}

// rules: STN-89
// expect: not-stone: loans: dereferences 'o.r', which may be empty
int swap_with_empty(Owner& o) {
  std::unique_ptr<R> e;
  o.r.swap(e);
  return o.r->value;
}

// Genom en kontextreferens: målet är samma objekt i båda anropen.
// rules: STN-89, STN-53
// expect: not-stone: loans: passes 'o.r', which may be empty, to 'Ctx::read', which assumes it is not
int through_context(Ctx& c) {
  c.kill();
  return c.read();
}

// rules: STN-89
// expect: not-stone: loans: passes 'a temporary.r', which may be empty, to 'deref', which assumes it is not
int temporary_empty() { return deref(Owner{}); }

struct Holder {
  std::unique_ptr<R> a;
  // Ett objekt kan vara flyttat från när det förstörs.
  // rules: STN-89, STN-68
  // expect: not-stone: loans: dereferences '*this.a', which may be empty
  ~Holder() { a->value = 0; }
};

#ifdef PORK_RUN
#include <pork/run.hpp>
static Owner full(int v) { return Owner{std::make_unique<R>(R{v}), true}; }
PORK_RUN_CASES(
    {"Owner::read", [] { Owner o = full(3); return o.read() == 3 ? 0 : 1; }},
    {"Owner::drop", [] { Owner o = full(3); o.drop(); return o.r ? 1 : 0; }},
    {"deref", [] { return deref(full(2)) == 2 ? 0 : 1; }},
    {"checked", [] { return checked(Owner{}) == 0 && checked(full(2)) == 2 ? 0 : 1; }},
    {"close", [] { Owner o = full(1); close(o); return o.r ? 1 : 0; }},
    {"Pair::Pair", [] { Pair p; return p.a && !p.b ? 0 : 1; }},
    {"maybe_make", [] { return maybe_make(true) && !maybe_make(false) ? 0 : 1; }},
    {"add_empty", [] { std::vector<std::unique_ptr<R>> v; add_empty(v); return v.size() == 1 && !v[0] ? 0 : 1; }},
    {"Ctx::kill", [] { Owner o = full(1); Ctx c{o}; c.kill(); return o.r ? 1 : 0; }},
    {"Ctx::read", [] { Owner o = full(6); Ctx c{o}; return c.read() == 6 ? 0 : 1; }},
    {"reset_then_check", [] { Owner o = full(1); return reset_then_check(o) == 0 ? 0 : 1; }},
    {"close_then_reassign", [] { Owner o = full(1); return close_then_reassign(o) == 2 ? 0 : 1; }},
    {"early_return", [] { Owner o = full(1); Owner q = full(4); return early_return(o, true) == 0 && early_return(q, false) == 4 ? 0 : 1; }},
    {"both", [] { auto a = std::make_unique<R>(R{1}); std::unique_ptr<R> b; return both(a, b) == 0 ? 0 : 1; }},
    {"constructed_half", [] { return constructed_half(1) == 2 ? 0 : 1; }},
    {"pushed", [] { return pushed() == 4 ? 0 : 1; }},
    {"get_checked", [] { Owner o = full(1); return get_checked(o) == 0 ? 0 : 1; }},
    {"element_by_reference", [] { std::vector<std::unique_ptr<R>> v; v.push_back(std::make_unique<R>(R{1})); v.push_back(std::make_unique<R>(R{7})); return element_by_reference(v) == 7 ? 0 : 1; }},
    {"swap_full", [] { Owner o = full(1); return swap_full(o) == 5 ? 0 : 1; }},
    {"temporary_full", [] { return temporary_full() == 1 ? 0 : 1; }})
#endif
