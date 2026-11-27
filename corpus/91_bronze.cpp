// Bronze (spec/3_bronze.txt): flödeskrav med PORK_DEPENDS mot det härledda
// flödet. Fallen är fristående exempel (ej publicerade) och
// poängkraven i rugby, varje krav med originalet och en
// avsiktlig ändring som ska fällas.
#include <optional>
#include <pork/annotations.hpp>
#include <cassert>
#include <vector>

// --- Fakturering: revisionen visar det ursprungliga priset ---------------------

// rules: BRZ-1, BRZ-2, BRZ-4, BRZ-5
// expect: bronze
int invoice(int price, bool discount, int& audit) {
  PORK_DEPENDS(audit, price);
  PORK_DEPENDS_RESULT(price, discount);
  audit = price;
  return discount ? price / 2 : price;
}

// rules: BRZ-2, BRZ-4, BRZ-6
// expect: stone: not bronze: audit depends on discount, which PORK_DEPENDS does not allow
// expect-violation
int invoice_mutated(int price, bool discount, int& audit) {
  PORK_DEPENDS(audit, price);
  PORK_DEPENDS_RESULT(price, discount);
  audit = discount ? price / 2 : price;
  return discount ? price / 2 : price;
}

// --- Kvottilldelning: kundtypen påverkar bara meddelandet ----------------------

struct Decision {
  int quota = 0;
  int message = 0;
};

// rules: BRZ-1, BRZ-2
// expect: bronze
void allocate(int requested, int limit, bool premium, Decision& decision) {
  PORK_DEPENDS(decision.quota, requested, limit);
  decision.quota = requested < limit ? requested : limit;
  decision.message = premium ? 2 : 1;
}

// rules: BRZ-2
// expect: stone: not bronze: decision.quota depends on premium, which PORK_DEPENDS does not allow
// expect-violation
void allocate_mutated(int requested, int limit, bool premium, Decision& decision) {
  PORK_DEPENDS(decision.quota, requested, limit);
  decision.quota = premium ? limit : (requested < limit ? requested : limit);
  decision.message = premium ? 2 : 1;
}

// --- Källval: prioriteten får inte nå identiteten, också via en hjälpfunktion -

// rules: BRZ-2
// expect: bronze
int select_source(int identity, int priority) {
  PORK_DEPENDS_RESULT(identity);
  (void)priority;
  return identity;
}
// rules: BRZ-2
// expect: bronze
int gateway(int priority, int identity) {
  PORK_DEPENDS_RESULT(identity);
  return select_source(identity, priority);
}
// rules: BRZ-2
// expect: stone
int select_source_mutated(int identity, int priority) { return priority > 0 ? priority : identity; }
// rules: BRZ-2
// expect: stone: not bronze: the result depends on priority, which PORK_DEPENDS does not allow
// expect-violation
int gateway_mutated(int priority, int identity) {
  PORK_DEPENDS_RESULT(identity);
  return select_source_mutated(identity, priority);
}

// --- Jobbstatus: status skrivs på varje normal retur ---------------------------

// rules: BRZ-2
// expect: bronze
void report_job(bool enabled, int& status) {
  PORK_DEPENDS(status, enabled);
  if (!enabled) {
    status = 0;
    return;
  }
  status = 1;
}
// Den tidiga returen lämnar status orörd: statusens ingångsvärde blir ett beroende.
// rules: BRZ-2
// expect: stone: not bronze: status depends on status, which PORK_DEPENDS does not allow
// expect-violation
void report_job_mutated(bool enabled, int& status) {
  PORK_DEPENDS(status, enabled);
  if (!enabled) return;
  status = 1;
}
// Med ingångsvärdet tillåtet håller kravet.
// rules: BRZ-2
// expect: bronze
void report_job_keeps(bool enabled, int& status) {
  PORK_DEPENDS(status, enabled, status);
  if (!enabled) return;
  status = 1;
}

// --- Rugby: poäng och avblåsning (förenklat) -----------------------------------

struct Vec2 { double x = 0; double y = 0; };
struct Pitch { double length = 100; double width = 70; };
struct Score { int home = 0; int away = 0; };
struct Player { double top_speed = 0; };
struct Whistle { int reason = 0; Vec2 position; };
struct Ball {
  std::optional<int> carrier;
  Vec2 position;
};
struct World {
  Pitch pitch;
  Ball ball;
  Score score;
  std::vector<Player> players;
  std::optional<Whistle> whistle;
};

// rules: BRZ-1, BRZ-2
// expect: bronze
bool in_goal(const Pitch& pitch, Vec2 p) {
  PORK_DEPENDS_RESULT(pitch, p.x);
  return p.x > pitch.length;
}
// rules: BRZ-2
// expect: stone: not bronze: the result depends on p.y, which PORK_DEPENDS does not allow
// expect-violation
bool in_goal_mutated(const Pitch& pitch, Vec2 p) {
  PORK_DEPENDS_RESULT(pitch, p.x);
  return p.x > pitch.length && p.y > 0;
}

// En plats täcker sina delar (world.pitch, world.ball). Avblåsningen och
// poängen kanske inte skrivs: deras ingångsvärden är tillåtna.
// rules: BRZ-1, BRZ-2
// expect: bronze: assuming std::optional::operator bool
void check_try(World& world) {
  PORK_PRE(world.score.home >= 0 && world.score.home <= 1000);
  PORK_DEPENDS(world.score.home, world.ball, world.pitch, world.score.home);
  PORK_DEPENDS(world.whistle, world.ball, world.pitch, world.whistle);
  if (world.ball.carrier && in_goal(world.pitch, world.ball.position)) {
    world.score.home += 5;
    world.whistle = Whistle{1, world.ball.position};
  }
}
// rules: BRZ-2
// expect: stone: not bronze: world.whistle depends on world.score.away, which PORK_DEPENDS does not allow
// expect-violation
void check_try_mutated(World& world) {
  PORK_PRE(world.score.home >= 0 && world.score.home <= 1000);
  PORK_DEPENDS(world.score.home, world.ball, world.pitch, world.score.home);
  PORK_DEPENDS(world.whistle, world.ball, world.pitch, world.whistle);
  if (world.ball.carrier && in_goal(world.pitch, world.ball.position)) {
    world.score.home += 5;
    world.whistle = Whistle{world.score.away > 0 ? 2 : 1, world.ball.position};
  }
}

// --- Kraven själva ---------------------------------------------------------------

// Ett krav på en indata är inget flödeskrav.
// rules: BRZ-1, BRZ-6
// expect: stone: not bronze: PORK_DEPENDS: 'price' is not an output
// expect-violation
int not_an_output(int price, int& audit) {
  PORK_DEPENDS(price, audit);
  audit = price;
  return price;
}
// Ett tillåtet värde måste vara en plats i en parameter.
// rules: BRZ-1
// expect: stone: not bronze: PORK_DEPENDS: argument 2 is not a parameter, a field path or a field of *this
// expect-violation
void not_a_place(int price, int& audit) {
  PORK_DEPENDS(audit, price + 1);
  audit = price;
}
// rules: BRZ-1, BRZ-6
// expect: not-stone: PORK_DEPENDS must come before all other statements
// expect-violation
void late(int price, int& audit) {
  audit = price;
  PORK_DEPENDS(audit, price);
}
// Ett krav utanför kontraktsinledningen håller inte, också där Stone inte
// ser det (BRZ-6): i en konstruktorinitierare, i sizeof, i en lambda.
struct Box {
  int v;
  // rules: BRZ-1, BRZ-6
  // expect: not-stone: PORK_DEPENDS must come before all other statements
  // expect-violation
  explicit Box(int x) : v((PORK_DEPENDS_RESULT(x), x)) {}
};
// rules: BRZ-1, BRZ-6
// expect: stone: not bronze: PORK_DEPENDS must come before all other statements
// expect-violation
int unevaluated(int x, int y) {
  PORK_DEPENDS_RESULT(x);
  (void)sizeof(PORK_DEPENDS_RESULT(y), 0);
  return 2 * x;
}
// rules: BRZ-1, BRZ-6
// expect: not-stone: in lambda 'f': PORK_DEPENDS in a lambda
// expect-violation
int in_lambda(int x) {
  auto f = [](int y) {
    PORK_DEPENDS_RESULT(y);
    return y;
  };
  return f(x);
}
// Efter en inledande assert: kravet står i inledningen och prövas.
// rules: BRZ-1
// expect: bronze
int after_assert(int x) {
  assert(x >= 0);
  PORK_DEPENDS_RESULT(x);
  return x + 1;
}
// Ett krav utanför varje funktionskropp hör inte till någon funktion: felet
// står där kravet står (BRZ-6), inte på funktionen som deklarerar
// defaultargumentet eller på anroparen som använder det. Anroparen kör
// uttrycket och är inte Stone, men har inget kravbrott.
// rules: BRZ-6
// expect: stone
// expect-violation
int scaled(int x, int k = (PORK_DEPENDS_RESULT(1), 2)) { return x * k; }
// rules: BRZ-6
// expect-violation
int declared_only(int x, int k = (PORK_DEPENDS_RESULT(1), 2));
struct Settings {
  int a = 1;
  // rules: BRZ-6
  // expect-violation
  int b = (PORK_DEPENDS_RESULT(a), 2);
};
// rules: BRZ-1
// expect: not-stone: PORK_DEPENDS must come before all other statements
int uses_settings() {
  Settings s;
  return s.a;
}
// rules: BRZ-6
// expect-violation
int stray_global = (PORK_DEPENDS_RESULT(1), 4);
// rules: BRZ-6
// expect-violation
static_assert((PORK_DEPENDS_RESULT(), true));
// rules: BRZ-6
// expect-violation
enum StrayEnum { stray_value = (PORK_DEPENDS_RESULT(), 1) };
// Placeringen i källan avgör, också i en mall som aldrig instansieras
// (där är anropet en olöst uppslagning).
// rules: BRZ-6
// expect-violation
template <class T> int stray_template_value = (PORK_DEPENDS_RESULT(T{}), 7);
template <class T>
struct StrayHolder {
  // rules: BRZ-6
  // expect-violation
  T v = (PORK_DEPENDS_RESULT(T{}), T{});
};
// Ett krav i en oanvänd funktionsmall står i en kropp: inget fel, också
// efter kontrakt och assert med beroende argument.
template <class T>
T unused_template(T x) {
  PORK_PRE(x > 0);
  PORK_POST(T r, r > 0);
  assert(x < 100);
  PORK_DEPENDS_RESULT(x);
  return x;
}
// Placeringen prövas på mallmönstret, också när mallen aldrig instansieras;
// flödet väntar på instanserna.
template <class T>
T unused_late(T x) {
  T copy = x;
  // rules: BRZ-6
  // expect-violation
  PORK_DEPENDS_RESULT(x);
  return copy;
}
template <class T>
struct UnusedBox {
  T v;
  T get() const {
    T c = v;
    // rules: BRZ-6
    // expect-violation
    PORK_DEPENDS_RESULT(v);
    return c;
  }
};
// En instansierad mall: felet står på instansen, inte på mönstret.
template <class T>
// rules: BRZ-6
// expect: not-stone: PORK_DEPENDS must come before all other statements
// expect-violation
T used_late(T x) {
  T copy = x;
  PORK_DEPENDS_RESULT(x);
  return copy;
}
template int used_late<int>(int);
// En instans där grenen med kravet väljs bort rapporterar inte platsen:
// mallens fel står kvar (placeringen gäller källan).
template <bool enabled>
// rules: BRZ-6
// expect: stone
int choose(int x) {
  if constexpr (enabled) {
    // rules: BRZ-6
    // expect-violation
    PORK_DEPENDS_RESULT(x);
  }
  return x;
}
template int choose<false>(int);
// Konstruktorer som använder fältinitieraren äger inte kravet: felet står
// en gång, vid fältet.
struct Counted {
  // rules: BRZ-6
  // expect-violation
  int count = (PORK_DEPENDS_RESULT(), 3);
  // rules: BRZ-1
  // expect: not-stone: PORK_DEPENDS must come before all other statements
  Counted() {}
};
// rules: BRZ-1
// expect: not-stone: calls non-stone 'Counted::Counted'
int makes_counted() {
  Counted c;
  return c.count;
}
// rules: BRZ-1
// expect: not-stone: PORK_DEPENDS must come before all other statements
int scaled_default(int x) { return scaled(x); }
// Ett krav i en lokal klass hör till metoden, inte till den omslutande
// funktionen (som inte är Stone av ett annat skäl, utan kravbrott).
// rules: BRZ-1
// expect: not-stone: unsupported local declaration: CXXRecord
int enclosing(int x) {
  struct Worker {
    // rules: BRZ-1
    // expect: bronze
    int compute(int y) const {
      PORK_DEPENDS_RESULT(y);
      return y;
    }
  };
  return Worker{}.compute(x);
}
// Ett krav i en funktion som inte är Stone håller inte (BRZ-6).
int global_rate = 3;
// rules: BRZ-6
// expect: not-stone: uses variable 'global_rate' with static storage
// expect-violation
int with_global(int price) {
  PORK_DEPENDS_RESULT(price);
  return price * global_rate;
}
// En tom tillåtelselista: utdatan är en konstant.
// rules: BRZ-1
// expect: bronze
void constant_out(int& out) {
  PORK_DEPENDS(out);
  out = 7;
}
// rules: BRZ-2, BRZ-6
// expect: stone: not bronze: out depends on price, which PORK_DEPENDS does not allow
// expect-violation
void not_constant_out(int price, int& out) {
  PORK_DEPENDS(out);
  out = price;
}
// Utan härlett flöde håller inget krav (här: en lokal med egen destruktor).
struct Guard {
  int v = 0;
  // rules: BRZ-4
  // expect: stone
  ~Guard() {}
};
// rules: BRZ-3, BRZ-6
// expect: stone: not bronze: flow not computed: destructor with user code
// expect-violation
int no_flow(int price) {
  PORK_DEPENDS_RESULT(price);
  Guard g{price};
  return g.v;
}
// Utan krav: Stone-domen gäller.
// rules: BRZ-4
// expect: stone
int no_requirement(int price, bool discount) { return discount ? price / 2 : price; }
