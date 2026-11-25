// Delat ägande (STN-88): std::shared_ptr och std::weak_ptr är aldrig värde-
// typer eller lånevärden, inte heller med `type std::shared_ptr` i trust-filen.
// Två kopior är samma objekt, så aliasregeln (G3) och lånen per väg gäller inte:
// keep(a, b) med b = a skriver genom a medan r lånar genom b.
#include <memory>
#include <vector>

struct Box {
  std::shared_ptr<std::vector<int>> v;
};

struct Plain {
  std::unique_ptr<int> p;
};

// Gränsfall: unique_ptr (STN-69) är fortfarande en värdetyp.
// rules: STN-88, STN-69
// expect: stone: assuming
int unique_owner(const Plain& x) { return x.p ? *x.p : 0; }

// rules: STN-88
// expect: not-stone: field 'v' of Box has type std::shared_ptr<std::vector<int>> (std::shared_ptr<std::vector<int>> shares ownership
int keep(Box& a, Box& b) {
  const int& r = (*b.v)[0];
  a.v->push_back(1);
  return r;
}

// rules: STN-88
// expect: not-stone: shares ownership
int read_only(const std::shared_ptr<const int>& p) { return p ? *p : 0; }

// rules: STN-88
// expect: not-stone: shares ownership
int local_copy() {
  auto p = std::make_shared<int>(3);
  auto q = p;
  return *q;
}

// Ett lånevärde som typargument gör den inte till ett lånevärde.
// rules: STN-88
// expect: not-stone: shares ownership
int shared_pointer_to_loan(const std::shared_ptr<int*>& p) { return **p; }

// rules: STN-88
// expect: not-stone: shares ownership
bool expired(const std::weak_ptr<int>& w) { return w.expired(); }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"unique_owner", [] { Plain x{std::make_unique<int>(2)}; return unique_owner(x) == 2 ? 0 : 1; }})
#endif
