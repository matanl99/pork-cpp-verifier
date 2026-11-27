// En standardinitierare som läser ett tidigare fält (STN-104): som STN-66 (c)
// för konstruktorinitierare. Fälten initieras i deklarationsordning, så ett
// tidigare aritmetiskt fält eller enumfält har ett värde; läsningen är en
// kopia. Ett senare fält (oinitierat), ett lån eller en referens in i
// objektet, och *this på annat sätt avvisas som förut (STN-92).
#include <span>
#include <string>

struct P {
  int a = 1;
  int b = a + 1;
};

// rules: STN-66, STN-104
// expect: stone
int default_constructed() {
  P p;
  return p.b;
}

// rules: STN-66, STN-104
// expect: stone
int aggregate_init() {
  P p{5};
  return p.b;
}

struct Q {
  int a;
  int b = a * 2;
  // rules: STN-66, STN-104
  // expect: stone
  explicit Q(int x) : a(x) {}
};

// rules: STN-66, STN-104
// expect: stone
int constructed(int x) {
  Q q(x);
  return q.b;
}

enum class Farg { rod, gron };

struct R {
  Farg f = Farg::gron;
  bool gron = f == Farg::gron;
};

// rules: STN-66, STN-104
// expect: stone
bool enum_field() {
  R r;
  return r.gron;
}

// Ett senare fält är inte initierat än.
struct Later {
  int b = a + 1;
  int a = 1;
};

// rules: STN-66, STN-104
// expect: not-stone: initializer of field 'b' uses *this
int later_field() {
  Later l;
  return l.b;
}

// Ett lån in i objektet (STN-92): en kopia skulle låna originalet.
struct View {
  std::string t = "abc";
  std::span<const char> v{t};
};

// rules: STN-92, STN-104
// expect: not-stone: initializer of field 'v' uses *this
int loan_into_object() {
  View w;
  return static_cast<int>(w.v.size());
}

// Ett tidigare fält utan initierare: vid standardinitiering är a obestämt.
struct Unset {
  int a;
  int b = a + 1;
};

// rules: STN-66, STN-104
// expect: not-stone: field 'a' of Unset is not initialized
int unset_earlier() {
  Unset u;
  return u.b;
}

// rules: STN-66, STN-104
// expect: stone
int unset_earlier_value_initialized() {
  Unset u{};
  return u.b;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"default_constructed", [] { return default_constructed() == 2 ? 0 : 1; }},
    {"aggregate_init", [] { return aggregate_init() == 6 ? 0 : 1; }},
    {"constructed", [] { return constructed(3) == 6 ? 0 : 1; }},
    {"enum_field", [] { return enum_field() ? 0 : 1; }},
    {"unset_earlier_value_initialized", [] { return unset_earlier_value_initialized() == 1 ? 0 : 1; }})
#endif
