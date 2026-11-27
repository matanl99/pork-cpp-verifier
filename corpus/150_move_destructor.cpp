// Flytt ur en klass med egen destruktor (STN-102): destruktorn körs när
// objektet dör och ser då det flyttade fältet. Den får inte kunna läsa den
// flyttade vägen (STN-57: ingen läser ett flyttat värde), oavsett om roten
// används efter flytten eller inte. Att röra andra fält går bra. Icke-virtuella
// metoder på *this granskas på samma sätt; en virtuell metod, *this som
// argument eller en metod utan kropp räknas som att läsa allt.
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

// nollstall() rör bara n: metodens kropp granskas mot samma väg.
// rules: STN-57, STN-102
// expect: stone: assuming
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

struct Kedja {
  std::string namn;
  int n;
  // rules: STN-12
  // expect: stone: assuming
  int langd() const { return int(namn.size()); }
  // rules: STN-12
  // expect: stone: assuming
  void spara() { n = langd(); }
  // rules: STN-68
  // expect: stone: assuming
  ~Kedja() { spara(); }
};

// Destruktorn -> spara() -> langd() läser namn.
// rules: STN-57, STN-102
// expect: not-stone: std::move(k.namn) out of Kedja, whose destructor may read it
std::string method_chain_reads() {
  Kedja k{"Anna", 0};
  return std::move(k.namn);
}

struct Varv {
  std::string namn;
  int n;
  // rules: STN-12
  // expect: stone: assuming
  void ett(int k) { if (k > 0) tva(k - 1); }
  // rules: STN-12
  // expect: stone: assuming
  void tva(int k) { n = k; if (k > 0) ett(k - 1); }
  // rules: STN-68
  // expect: stone: assuming
  ~Varv() { ett(3); }
};

// Metoder som anropar varandra: varje kropp granskas en gång.
// rules: STN-57, STN-102
// expect: stone: assuming
std::string mutual_methods() {
  Varv v{"Anna", 0};
  return std::move(v.namn);
}

// rules: STN-12
// expect: stone: assuming
int langd_av(const Rensad& r) { return int(r.namn.size()); }

struct Skickar {
  Rensad r;
  int n;
  // rules: STN-68
  // expect: stone: assuming
  ~Skickar() { n = langd_av(r); }
};

// Hela r skickas vidare: r rymmer den flyttade vägen.
// rules: STN-57, STN-102
// expect: not-stone: std::move(s.r.namn) out of Skickar, whose destructor may read it
std::string field_passed_on() {
  Skickar s{{"Anna", 1}, 0};
  return std::move(s.r.namn);
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
    {"sibling_in_destructor", [] { return sibling_in_destructor() == "Anna" ? 0 : 1; }},
    {"method_in_destructor", [] { return method_in_destructor() == "Anna" ? 0 : 1; }},
    {"mutual_methods", [] { return mutual_methods() == "Anna" ? 0 : 1; }})
#endif
