// Flytt av std::unique_ptr (STN-90): en flyttad unique_ptr är tom, och
// lånemodellen följer tomheten (STN-89). Därför får vilken väg som helst
// flyttas (ett fält, ett element) och kontrolleras efteråt, och ägandet får
// lämnas över direkt med t{o.release()} eller t.reset(o.release()).
#include <memory>
#include <utility>
#include <vector>

struct R {
  int value;
};

struct Owner {
  std::unique_ptr<R> r;
  bool on;
};

// rules: STN-90
// expect: stone: assuming
int take(std::unique_ptr<R> p) { return p ? p->value : 0; }

// --- godtas -----------------------------------------------------------------

// rules: STN-90
// expect: stone: assuming
int from_field(Owner& o) {
  auto x = std::move(o.r);
  return x->value;
}

// rules: STN-90
// expect: stone: assuming
int field_checked_after(Owner& o) {
  auto x = std::move(o.r);
  return (o.r ? 1 : 0) + x->value;
}

// rules: STN-90
// expect: stone: assuming
int into_call(Owner& o) { return take(std::move(o.r)); }

// Ett annat fält i samma uttryck överlappar inte o.r.
// rules: STN-90
// expect: stone: assuming
int other_field_same_expression(Owner& o) { return (o.on ? 1 : 0) + take(std::move(o.r)); }

// rules: STN-90, STN-89
// expect: stone: assuming
int out_of_vector(int value) {
  std::vector<std::unique_ptr<R>> v;
  v.push_back(std::make_unique<R>(R{value}));
  v.push_back(std::make_unique<R>(R{0}));
  auto x = std::move(v.front());
  v.erase(v.begin());
  return x->value;
}

// rules: STN-90
// expect: stone: assuming
int local_checked_after() {
  auto p = std::make_unique<R>(R{1});
  int n = take(std::move(p));
  // analyzer: cplusplus.Move: a moved-from unique_ptr is null ([unique.ptr.single.ctor])
  return p.get() == nullptr ? n : 0;
}

// rules: STN-90
// expect: stone: assuming
int local_reassigned() {
  auto p = std::make_unique<R>(R{1});
  int n = take(std::move(p));
  p = std::make_unique<R>(R{2});
  return p->value + n;
}

// rules: STN-90
// expect: stone: assuming
int each_element(std::vector<std::unique_ptr<R>>& v) {
  int n = 0;
  for (auto& e : v) n += take(std::move(e));
  return n;
}

// rules: STN-90
// expect: stone: assuming
int release_to_new_owner(Owner& o) {
  std::unique_ptr<R> t{o.r.release()};
  return t->value + (o.r ? 1 : 0);
}

// rules: STN-90
// expect: stone: assuming
int reset_from_release(Owner& o, std::unique_ptr<R>& t) {
  t.reset(o.r.release());
  return t->value;
}

// --- underkänns -------------------------------------------------------------

// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 'o.r', which may be empty
int field_used_after(Owner& o) {
  auto x = std::move(o.r);
  return o.r->value + x->value;
}

// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 'p', which may be empty
int local_used_after() {
  auto p = std::make_unique<R>(R{1});
  auto q = std::move(p);
  return p->value + q->value;
}

// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 'p', which may be empty
int parameter_used_after(std::unique_ptr<R>& p) {
  int n = take(std::move(p));
  return p->value + n;
}

// v[0] och v[1] är samma plats v[*].
// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 'v[*]', which may be empty
int other_element_after(std::vector<std::unique_ptr<R>>& v) {
  auto x = std::move(v[0]);
  return v[1]->value + (x ? 1 : 0);
}

// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 'v[*]', which may be empty
int elements_after_loop(std::vector<std::unique_ptr<R>>& v) {
  int n = 0;
  for (auto& e : v) n += take(std::move(e));
  return v[0]->value + n;
}

// rules: STN-90
// expect: not-stone: 'o.r' is used in the same expression as std::move(o.r)
int same_expression(Owner& o) { return take(std::move(o.r)) + (o.r ? 1 : 0); }

// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 'o.r', which may be empty
int source_after_release(Owner& o) {
  std::unique_ptr<R> t{o.r.release()};
  return o.r->value + (t ? 1 : 0);
}

// rules: STN-90, STN-89
// expect: not-stone: loans: dereferences 't', which may be empty
int release_of_empty(std::unique_ptr<R>& t) {
  std::unique_ptr<R> e;
  t.reset(e.release());
  return t->value;
}

// Pekaren passerar en lokal variabel: inte en direkt överlämning.
// rules: STN-90, STN-69
// expect: not-stone: releases ownership to a raw pointer
int release_through_local(Owner& o) {
  R* raw = o.r.release();
  std::unique_ptr<R> t{raw};
  return t->value;
}

// rules: STN-90, STN-69
// expect: not-stone: releases ownership to a raw pointer
void release_dropped(Owner& o) { o.r.release(); }

#ifdef PORK_RUN
#include <pork/run.hpp>
static Owner full(int v) { return Owner{std::make_unique<R>(R{v}), true}; }
PORK_RUN_CASES(
    {"take", [] { return take(std::make_unique<R>(R{2})) == 2 && take(nullptr) == 0 ? 0 : 1; }},
    {"from_field", [] { Owner o = full(3); return from_field(o) == 3 && !o.r ? 0 : 1; }},
    {"field_checked_after", [] { Owner o = full(3); return field_checked_after(o) == 3 ? 0 : 1; }},
    {"into_call", [] { Owner o = full(4); return into_call(o) == 4 ? 0 : 1; }},
    {"other_field_same_expression", [] { Owner o = full(4); return other_field_same_expression(o) == 5 ? 0 : 1; }},
    {"out_of_vector", [] { return out_of_vector(7) == 7 ? 0 : 1; }},
    {"local_checked_after", [] { return local_checked_after() == 1 ? 0 : 1; }},
    {"local_reassigned", [] { return local_reassigned() == 3 ? 0 : 1; }},
    {"each_element", [] { std::vector<std::unique_ptr<R>> v; v.push_back(std::make_unique<R>(R{1})); v.push_back(std::make_unique<R>(R{2})); return each_element(v) == 3 && !v[0] ? 0 : 1; }},
    {"release_to_new_owner", [] { Owner o = full(5); return release_to_new_owner(o) == 5 ? 0 : 1; }},
    {"reset_from_release", [] { Owner o = full(6); std::unique_ptr<R> t; return reset_from_release(o, t) == 6 && !o.r ? 0 : 1; }})
#endif
