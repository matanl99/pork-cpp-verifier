// STN-69: unique_ptr<Base> från unique_ptr<Derived> förstör sedan objektet
// genom ~Base, vilket är odefinierat när den inte är virtuell
// ([expr.delete]/3). Förut Stone (granskning av unique_ptr,
// check_base_owner); ASan ser det som new-delete-type-mismatch.
#include <memory>
#include <utility>

struct Base { int value = 2; };
struct Derived : Base { int extra = 4; };

// rules: STN-69
// expect: not-stone: moves a std::unique_ptr<Derived> into a std::unique_ptr<Base>, whose destructor is not virtual
int bad_construct() {
  auto p = std::make_unique<Derived>();
  std::unique_ptr<Base> base = std::move(p);
  return base->value;
}

// rules: STN-69
// expect: not-stone: whose destructor is not virtual
int bad_assign() {
  std::unique_ptr<Base> base;
  base = std::make_unique<Derived>();
  return base ? base->value : 0;
}

// rules: STN-69
// expect: not-stone: whose destructor is not virtual
std::unique_ptr<Base> bad_return() { return std::make_unique<Derived>(); }

// Samma elementtyp, och T -> const T: ingen typändring vid destruktionen.
// rules: STN-69
// expect: stone: assuming
int same_type() {
  auto p = std::make_unique<Base>();
  std::unique_ptr<Base> q = std::move(p);
  return q->value;
}

// rules: STN-69
// expect: stone: assuming
int to_const() {
  auto p = std::make_unique<int>(3);
  std::unique_ptr<const int> q = std::move(p);
  return *q;
}

#ifdef PORK_CONSTEXPR
#include <pork/constexpr.hpp>
PORK_CONSTEXPR_CASE("bad_assign", return bad_assign() == 2;)
#endif

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"bad_construct", [] { return bad_construct() == 2 ? 0 : 1; }},
    {"same_type", [] { return same_type() == 2 ? 0 : 1; }},
    {"to_const", [] { return to_const() == 3 ? 0 : 1; }})
#endif
