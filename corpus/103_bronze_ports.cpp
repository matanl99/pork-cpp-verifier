// Bronze och portar (BRZ-2). Ett anrop genom en port ger ett beroende av
// porten. En tillåten port täcker allt den når, också genom referenser som
// dess implementationer håller: Leak håller en referens till Ctx, så resultatet
// av allowed_port kan ändras när bara c ändras. Det är tillåtet, eftersom c nås
// genom k.

#include <pork/annotations.hpp>

struct Ctx {
  int secret;
};

struct IClock {
  virtual int now() const = 0;
  // rules: STN-52
  // expect: stone
  virtual ~IClock() = default;
};

struct Leak : IClock {
  const Ctx& c_;
  // rules: STN-53, STN-66
  // expect: stone
  explicit Leak(const Ctx& c) : c_(c) {}
  // rules: STN-52
  // expect: stone
  int now() const override { return c_.secret; }
};

// rules: BRZ-2
// expect: bronze: assuming dispatch IClock::now
int allowed_port(const IClock& k, const Ctx& c, int a) {
  PORK_DEPENDS_RESULT(a, k);
  int t = k.now();
  return a + t;
}

// rules: BRZ-2, BRZ-6
// expect: stone: not bronze: the result depends on k, which PORK_DEPENDS does not allow
// expect-violation
int excluded_port(const IClock& k, const Ctx& c, int a) {
  PORK_DEPENDS_RESULT(a, c);
  int t = k.now();
  return a + t;
}

// rules: BRZ-2, BRZ-6
// expect: stone: not bronze: the result depends on c.secret, which PORK_DEPENDS does not allow
// expect-violation
int excluded_ctx(const IClock& k, const Ctx& c, int a) {
  PORK_DEPENDS_RESULT(a, k);
  return a + c.secret;
}
