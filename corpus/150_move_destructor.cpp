// Flytt ur en klass med egen destruktor (STN-102): destruktorn körs när
// objektet dör och ser då det flyttade fältet. Den får inte kunna läsa den
// flyttade vägen (STN-57: ingen läser ett flyttat värde), oavsett om roten
// används efter flytten eller inte. Att röra andra fält går bra; att anropa en
// metod eller använda *this på annat sätt räknas som att läsa allt.
#include <string>
#include <utility>
#include <vector>

struct Namn {
  std::string namn;
  int alder;
  // rules: STN-68
  // expect: stone: assuming
  ~Namn() { alder = 0; }
};

// Destruktorn körs efter att returvärdet räknats ut: 30 + 4.
// rules: STN-96, STN-102
// expect: stone: assuming
int live_root() {
  Namn n{"Anna", 30};
  std::string s = std::move(n.namn);
  return n.alder + int(s.size());
}

// rules: STN-57, STN-102
// expect: stone: assuming
std::string dead_root() {
  Namn n{"Anna", 30};
  return std::move(n.namn);
}

struct Loggad {
  std::string namn;
  int langd;
  // rules: STN-68
  // expect: stone: assuming
  ~Loggad() { langd = int(namn.size()); }
};

// rules: STN-57, STN-102
// expect: not-stone: std::move(l.namn) out of Loggad, whose destructor may read it
std::string read_by_destructor() {
  Loggad l{"Anna", 0};
  return std::move(l.namn);
}

// rules: STN-96, STN-102
// expect: not-stone: std::move(l.namn) out of Loggad, whose destructor may read it
int read_by_destructor_live() {
  Loggad l{"Anna", 0};
  std::string s = std::move(l.namn);
  return l.langd + int(s.size());
}

struct Rensad {
  std::string namn;
  int n;
  // rules: STN-12
  // expect: stone: assuming
  void nollstall() { n = 0; }
  // rules: STN-68
  // expect: stone: assuming
  ~Rensad() { nollstall(); }
};

// En metod på *this kan läsa vad som helst.
// rules: STN-57, STN-102
// expect: not-stone: std::move(r.namn) out of Rensad, whose destructor may read it
std::string method_in_destructor() {
  Rensad r{"Anna", 1};
  return std::move(r.namn);
}

struct Inre {
  std::string namn;
  std::string stad;
};

struct Hushall {
  Inre inre;
  int antal;
  // rules: STN-68
  // expect: stone: assuming
  ~Hushall() { antal = int(inre.stad.size()); }
};

// Destruktorn läser h.inre.stad, inte h.inre.namn.
// rules: STN-57, STN-102
// expect: stone: assuming
std::string sibling_in_destructor() {
  Hushall h{{"Anna", "Lund"}, 1};
  return std::move(h.inre.namn);
}

struct Helt {
  Inre inre;
  int antal;
  // rules: STN-68
  // expect: stone: assuming
  ~Helt() { Inre kopia = inre; antal = int(kopia.namn.size()); }
};

// Destruktorn läser hela h.inre, som rymmer den flyttade vägen.
// rules: STN-57, STN-102
// expect: not-stone: std::move(h.inre.namn) out of Helt, whose destructor may read it
std::string whole_in_destructor() {
  Helt h{{"Anna", "Lund"}, 1};
  return std::move(h.inre.namn);
}

struct Bas {
  std::string namn;
  int langd;
  // rules: STN-68
  // expect: stone: assuming
  ~Bas() { langd = int(namn.size()); }
};

struct Harledd : Bas {
  int extra;
};

// Basens destruktor körs också och läser namn.
// rules: STN-57, STN-102
// expect: not-stone: std::move(h.namn) out of Harledd, whose destructor may read it
std::string base_destructor() {
  Harledd h{{"Anna", 0}, 1};
  return std::move(h.namn);
}

// Elementets destruktor körs när vektorn dör. I dag avvisas formen redan av
// sidoeffektsregeln (v[0] inne i std::move); STN-102 ser också elementets klass.
// rules: STN-21, STN-102
// expect: not-stone: side effect inside expression
std::string from_element(std::vector<Loggad> v) {
  std::string n = std::move(v[0].namn);
  return n;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"live_root", [] { return live_root() == 34 ? 0 : 1; }},
    {"dead_root", [] { return dead_root() == "Anna" ? 0 : 1; }},
    {"sibling_in_destructor", [] { return sibling_in_destructor() == "Anna" ? 0 : 1; }})
#endif
