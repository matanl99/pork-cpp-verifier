// pork-args: --silver
//
// Kontextreferenser: ett fält `T&` eller `const T&` där T är en värdetyp
// (`Context& game_`). Klassen är en objekttyp (STN-50) och fältet är ett dolt
// referensargument till varje metod. Anroparen ser det inte, så
//  - ett referensargument som kan nå T får inte stå i samma anrop som ett
//    objekt med en dold T& (utom när båda bara läser),
//  - ett anrop som kan skriva genom en dold T& (även en const-metod) är en
//    sidoeffekt (bara som sats, STN-20),
//  - två referensfält i samma klass får inte kunna peka på samma objekt.
// Silver: fältet är en pekare, harnessen pekar på ett eget objekt med
// godtyckligt innehåll (Stone garanterar att det är skilt från allt annat).

#include <pork/annotations.hpp>

struct Context { int x = 0; int dx = 0; };
struct World { Context c; int t; };
struct IPort { virtual ~IPort() = default; virtual void ping() = 0; };

struct Mover {
  Context& game_;
  IPort& port_;

  // rules: STN-53, SLV-5
  // expect: stone: not silver: arithmetic overflow on signed +
  void step() { game_.x = game_.x + game_.dx; }

  // rules: STN-53, SLV-36, SLV-17
  // expect: silver
  void safe_step() { PORK_PRE(game_.dx == 1 && game_.x < 100); game_.x = game_.x + game_.dx; }

  // rules: STN-53, SLV-36
  // expect: silver
  int peek() const { return game_.x; }

  // En const-metod kan skriva genom referensen.
  // rules: STN-53, SLV-36
  // expect: silver
  void bump() const { game_.x = 1; }

  // rules: STN-53, SLV-36
  // expect: silver
  void copy_to(Context& other) { other.x = game_.x; }

  // rules: STN-53
  // expect: not-stone: argument 'game_' may refer to the same object as reference field 'Mover::game_' of '*this'
  void copy_to_self() { copy_to(game_); }
};

// rules: STN-53
// expect: not-stone: Two has reference fields 'a_' and 'b_' that may refer to the same object
struct Two { Context& a_; Context& b_; void f() { a_.x = 1; } };

struct Reader {
  const Context& game_;
  // rules: STN-53, SLV-36
  // expect: silver
  int get() const { return game_.x; }
};

// Basklassens fält, även referensen, nås från den härledda klassen.
struct Base {
  // rules: STN-66, STN-53, SLV-39
  // expect: silver
  explicit Base(Context& g) : game_(g), steps(0) {}
  // rules: STN-50, SLV-36
  // expect: silver
  virtual ~Base() = default;
  Context& game_;
  int steps;
};
struct Derived : Base {
  // rules: STN-53, SLV-36
  // expect: silver
  void reset() { game_.x = 0; steps = 0; }
};

// rules: STN-53, SLV-17
// expect: stone: not silver: precondition of 'Mover::safe_step'
void drive(Mover& m) { m.safe_step(); }

// rules: STN-53, SLV-36
// expect: silver
void drive_checked(Mover& m) { PORK_PRE(m.game_.dx == 1 && m.game_.x < 100); m.safe_step(); }

// Här får m.game_ och c antas vara olika: anroparen har kontrollerats.
// rules: STN-53, SLV-36, SLV-18
// expect: silver
int distinct(Mover& m, Context& c) { PORK_POST(int r, r == 7); c.x = 7; m.bump(); return c.x; }

// rules: STN-53
// expect: not-stone: argument 'c' may refer to the same object as reference field 'Mover::game_' of 'm'
// loans: rejected: may be the same object
int call_distinct(Mover& m, Context& c) { return distinct(m, c); }

// Ett objekt som innehåller en Context.
// rules: STN-53
// expect: not-stone: argument 'w.c' may refer to the same object as reference field 'Mover::game_' of 'm'
void with_world(Mover& m, World& w) { m.copy_to(w.c); }

// Två objekt med varsin dold Context&: i pair får de antas vara olika.
// rules: STN-53, SLV-36
// expect: silver
void pair(Mover& a, Mover& b) { a.bump(); b.bump(); }

// rules: STN-53
// expect: not-stone: argument 'b' may refer to the same object as reference field 'Mover::game_' of 'a'
void call_pair(Mover& a, Mover& b) { pair(a, b); }

// Båda läser: tillåtet.
// rules: STN-53, SLV-36
// expect: silver
int both_read(const Reader& r, const Context& c) { int a = r.get(); return a / 2 + c.x / 2; }

// rules: STN-53, SLV-36
// expect: silver
int call_both_read(const Reader& r, const Context& c) { return both_read(r, c); }

// rules: STN-53, STN-20
// expect: not-stone: call to 'Mover::peek' may modify through reference field 'Mover::game_' of 'm'
int peek_in_expression(const Mover& m) { return m.peek() + 1; }

// rules: STN-53, SLV-36
// expect: silver
int peek_statement(const Mover& m) { int v = m.peek(); return v / 2; }

// rules: STN-53, SLV-36
// expect: silver
int read_in_expression(const Reader& r) { return r.get() / 2; }

// Basens fält som argument: en väg i *this.
struct Named {
  int id;
  // rules: STN-50
  // expect: silver
  virtual ~Named() = default;
};
struct Shown : Named {
  IPort& port_;
  Context& game_;
  // rules: STN-53, STN-62, SLV-36
  // expect: silver
  void show() { copy(id, game_); }
  // rules: STN-12
  // expect: silver
  static void copy(const int& v, Context& c) { c.x = v; }
};

// --- Anrop som når en port (STN-54) ---
// Implementationen bakom en port kan hålla referenser till vad som helst
// utanför funktionen (satta i en konstruktor) och anropa tillbaka. Ett sådant
// anrop är alltid en sidoeffekt, och inget lån in i annat än en lokal variabel
// får leva över det.

#include <optional>
#include <vector>

struct Cell { int v; };
struct Board { std::vector<Cell> cells; Cell spare = Cell{0}; };
struct ISink { virtual ~ISink() = default; virtual void flush() = 0; virtual int level() const = 0; virtual std::optional<int> poll() = 0; };

// rules: STN-54, STN-52
// expect: not-stone: reaches a polymorphic object and may change
// loans: rejected: reaches a polymorphic object
void hole(Board& b, ISink& s) { Cell& c = b.cells[0]; s.flush(); c.v = 1; }

// Lånet är dött före anropet.
// rules: STN-54, STN-62
// expect: stone: assuming
void dead_before(Board& b, ISink& s) { Cell& c = b.cells[0]; c.v = 1; s.flush(); }

// En lokal variabel når porten inte.
// rules: STN-54
// expect: stone: assuming
void local_board(ISink& s) { Board b; b.cells.push_back(Cell{0}); Cell& c = b.cells[0]; s.flush(); c.v = 1; }

// Ett fält (en väg, inget lån) kan inte dingla.
// rules: STN-54, STN-31
// expect: stone: assuming
void field_alias(Board& b, ISink& s) { Cell& c = b.spare; s.flush(); c.v = 1; }

// Även en const-metod på en port.
// rules: STN-54, STN-20
// expect: not-stone: side effect inside expression: call to 'ISink::level' reaches a port
int level_in_expression(const ISink& s) { return s.level() + 1; }

// rules: STN-54
// expect: stone: assuming
int level_statement(const ISink& s) { int v = s.level(); return v; }

// Hela villkorsvariabeln: tillåtet.
// rules: STN-54
// expect: stone: assuming
int drain(ISink& s) { int n = 0; while (auto e = s.poll()) { n = *e; } return n; }

// rules: STN-54
// expect: stone: assuming
void flush_sink(ISink& s) { s.flush(); }

// Indirekt: en funktion som får porten som argument.
// rules: STN-54
// expect: not-stone: reaches a polymorphic object and may change
void hole_via(Board& b, ISink& s) { Cell& c = b.cells[0]; flush_sink(s); c.v = 1; }

// Indirekt: ett objekt med en portreferens.
struct Flusher {
  ISink& sink_;
  // rules: STN-54
  // expect: stone: assuming
  void run() { sink_.flush(); }
};

// rules: STN-54
// expect: not-stone: reaches a polymorphic object and may change
void hole_holder(Board& b, Flusher& f) { Cell& c = b.cells[0]; f.run(); c.v = 1; }

// --- Vägar genom en kontextreferens ---
// `h.ref_` ligger i ett okänt objekt: det kan vara en del av varje annat
// argument som når samma typ.
struct Holder { Context& ref_; };
struct Box { Context c; };

// rules: STN-12
// expect: silver
void copy_ctx(const Context& a, Context& b) { b.x = a.x; }

// rules: STN-53
// expect: not-stone: arguments 'h.ref_' (through reference field 'Holder::ref_') and 'b.c' may refer to the same object
void through(Holder& h, Box& b) { copy_ctx(h.ref_, b.c); }

// Samma referens: vägarna avgör.
// rules: STN-53, STN-62
// expect: not-stone: may be the same object
void through_same(Holder& h) { copy_ctx(h.ref_, h.ref_); }

// Ett anrop av en funktion med PORK_POST (kontraktsversionen): även en
// const-metod kan ha skrivit genom kontextreferensen.
struct Poker {
  Context& game_;
  // rules: SLV-36, SLV-18
  // expect: silver
  int poke() const { PORK_POST(int r, r == 1); game_.x = 0; return 1; }
};

// rules: SLV-36, SLV-18
// expect: stone: not silver: division by zero
int after_poke(const Poker& p) { PORK_PRE(p.game_.x == 1); int r = p.poke(); return 10 / p.game_.x + r; }
