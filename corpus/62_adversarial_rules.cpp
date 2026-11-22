// Motståndargenomgång: fall som försöker ta sig förbi Stone.
// bad_* ska vara not-stone, ok_* stone (övriga är hjälpfunktioner med
// dagens dom). Förväntningarna genererades från porks domar efter att
// varje fall granskats; två hål hittades och tätades (implicit
// standardkonstruktor, cleanup-attribut, se 55_constructors och
// 58_destructors). Lägg till nya försök här när en regel ändras.
#include <span>
#include <utility>
#include <variant>
#include <vector>

int global = 0;

// ---- virtual / ports ----
// rules: STN-23
// expect: stone
struct IPort { virtual ~IPort() = default; virtual int get() const = 0; virtual void put(int v) = 0; };
// rules: STN-23
// expect: GoodImpl::get: stone
// expect: GoodImpl::put: stone
struct GoodImpl final : IPort { int v = 0; int get() const override { return v; } void put(int x) override { v = x; } };
// rules: STN-23
// expect: BadImpl::get: not-stone
// expect: BadImpl::put: stone
struct BadImpl final : IPort { int get() const override { return global; } void put(int) override {} };
// rules: STN-52
// expect: not-stone
int bad_dispatch(const IPort& p) { return p.get(); }  // BadImpl known -> not stone

// base-qualified call to a non-stone base implementation
// rules: STN-23
// expect: Base::~Base: stone
// expect: Base::f: not-stone
struct Base { virtual ~Base() = default; virtual int f() const { return global; } };
// rules: STN-23
// expect: stone
struct Der : Base { int f() const override { return 1; } };
// rules: STN-52
// expect: not-stone
int bad_qualified(const Der& d) { return d.Base::f(); }

// virtual call inside constructor resolves statically (Base::f)
// rules: STN-23
// expect: CallsInCtor::CallsInCtor: stone: assuming
// expect: CallsInCtor::f: stone
struct CallsInCtor : Base { int r; CallsInCtor() : r(0) { r = f(); } int f() const override { return 2; } };

// ---- context references ----
struct Ctx { int x = 0; };
struct User {
  Ctx& c_;
  // rules: STN-23
  // expect: stone
  explicit User(Ctx& c) : c_(c) {}
  // rules: STN-23
  // expect: stone
  void bump(int& y) { c_.x = 1; y = 2; }
};
// rules: STN-53
// expect: not-stone
void bad_context_alias(User& u, Ctx& c) { u.bump(c.x); }  // c may be u.c_

// ---- lambdas ----
// rules: STN-40
// expect: not-stone
int bad_lambda_ref_escape(std::vector<int>& v) {
  int& first = v[0];
  auto f = [&v]() { v.push_back(1); };
  f();
  return first;
}
// rules: STN-40
// expect: not-stone
int bad_lambda_global() { auto f = []() { global = 1; }; f(); return 0; }
// rules: STN-40
// expect: not-stone
int bad_lambda_static() { auto f = []() { static int n = 0; return ++n; }; return f(); }
// rules: STN-40
// expect: stone
int ok_lambda_local(int a) { auto f = [a](int b) { return a + b; }; return f(1); }

// ---- variant / visit ----
using Var = std::variant<int, float>;
// rules: STN-40
// expect: not-stone
int bad_visit_global(const Var& v) { return std::visit([](auto x) { return static_cast<int>(x) + global; }, v); }
// rules: STN-39, STN-62
// expect: not-stone
int bad_get_if_after_assign(Var& v) {
  int* p = std::get_if<int>(&v);
  v = 1.0F;
  return p != nullptr ? *p : 0;
}
// rules: STN-39, STN-62
// expect: stone: assuming
int ok_get_if(const Var& v) { const int* p = std::get_if<int>(&v); return p != nullptr ? *p : 0; }

// ---- move ----
// rules: STN-57
// expect: not-stone
int bad_use_after_move(std::vector<int>& v) {
  std::vector<int> w = std::move(v);
  return static_cast<int>(v.size() + w.size());
}

// ---- span ----
// rules: STN-62
// expect: not-stone
int bad_span_after_push(std::vector<int>& v) {
  std::span<const int> s = v;
  v.push_back(1);
  return s.empty() ? 0 : s[0];
}
// rules: STN-41, STN-62
// expect: stone
int ok_span_param(std::span<const int> s) { return s.empty() ? 0 : s[0]; }

// ---- structured bindings ----
struct PairI { int a; int b; };
// rules: STN-36
// expect: not-stone
int bad_binding_dangling(std::vector<PairI>& v) {
  auto& [a, b] = v[0];
  v.clear();
  return a + b;
}

// ---- templates: only the instantiation counts ----
// rules: STN-23
// expect: not-stone
template <typename T> int sizeish(const T& t) { return static_cast<int>(t.size()) + T::extra(); }
// rules: STN-23
// expect: WithExtra::size: stone
// expect: WithExtra::extra: not-stone
struct WithExtra { int size() const { return 1; } static int extra() { return global; } };
// rules: STN-6
// expect: not-stone
int bad_template_inst(const WithExtra& w) { return sizeish(w); }

// ---- statements ----
// rules: STN-13
// expect: not-stone
int bad_goto(int a) { if (a > 0) goto end; a = 1; end: return a; }
// rules: STN-14
// expect: not-stone
int bad_switch_static(int a) { switch (a) { case 1: { static int s = 0; return s; } default: return 0; } }

// ---- assert/PRE side effects ----
#include <cassert>
// rules: STN-49
// expect: not-stone
int bad_assert_effect(int& a) { assert((a = 1) > 0); return a; }
