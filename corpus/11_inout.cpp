// T& (in out) och icke-const-metoder: tillåtna, som i SPARK, med två regler:
//  - anti-aliasing: ett T&-argument måste vara en variabel/ett fält och får
//    inte överlappa ett annat referensargument eller tilldelningsmålet,
//  - ett anrop som ändrar sina argument är en sidoeffekt: bara som egen sats,
//    hela högerledet i en tilldelning, hela initieraren eller hela returvärdet.

struct Vec2 { int x; int y; };

struct Account {
  int balance;
  int limit;
  // rules: STN-7, STN-12
  // expect: stone
  void deposit(int amount) { balance += amount; }
  // rules: STN-7, STN-12
  // expect: stone
  void transfer_from(Account& other, int amount) { other.balance -= amount; balance += amount; }
  // rules: STN-7, STN-12
  // expect: stone
  void absorb(const Account& other) { balance += other.balance; }
};

// rules: STN-12
// expect: stone
void inc(int& a) { a += 1; }

// rules: STN-12
// expect: stone
void swap_ints(int& a, int& b) { int t = a; a = b; b = t; }

// rules: STN-12
// expect: stone
void move_by(Vec2& p, const Vec2& d) { p.x += d.x; p.y += d.y; }

// rules: STN-12
// expect: stone
void move_x(Vec2& p, int& x) { p.x += x; }

// rules: STN-12
// expect: stone
int add_and_return(int& a) { a += 1; return a; }

// --- Godkända anrop ---

// rules: STN-21, STN-26
// expect: stone
int twice_inc(int a) { inc(a); inc(a); return a; }

// rules: STN-21, STN-26
// expect: stone
void forward(int& a) { inc(a); }

// rules: STN-21
// expect: stone
int effect_contexts(int a) { int r = add_and_return(a); int b = 0; b = add_and_return(a); return r + b; }

// rules: STN-21
// expect: stone
int return_effect(int a) { return add_and_return(a); }

// rules: STN-21, STN-28
// expect: stone
Vec2 shifted(Vec2 p) { move_by(p, Vec2{1, 1}); return p; }

// rules: STN-26, STN-62
// expect: stone
int distinct_fields(Vec2 p) { swap_ints(p.x, p.y); return p.x; }

// rules: STN-21, STN-62
// expect: stone
int accounts(Account a, Account b) { a.transfer_from(b, 5); a.deposit(1); return a.balance + b.balance; }

// --- Aliasing ---

// rules: STN-62
// expect: not-stone: may be the same object
int alias_same(int a) { swap_ints(a, a); return a; }

// rules: STN-62
// expect: not-stone: may be the same object
int alias_const_ref(Vec2 p) { move_by(p, p); return p.x; }

// rules: STN-62
// expect: not-stone: may be the same object
int alias_prefix(Vec2 p) { move_x(p, p.x); return p.x; }

// rules: STN-62
// expect: not-stone: may be the same object
int alias_object(Account a) { a.transfer_from(a, 1); return a.balance; }

// rules: STN-62
// expect: not-stone: may be the same object
int alias_object_const_ref(Account a) { a.absorb(a); return a.balance; }

// rules: STN-62
// expect: stone
int alias_target(int a) { a = add_and_return(a); return a; }

// rules: STN-26
// expect: not-stone: argument for a non-const reference must be a variable or a field
int not_a_path(int a) { int b = 0; inc(a > 0 ? a : b); return a + b; }

// --- Sidoeffekter inuti uttryck ---

// rules: STN-21
// expect: not-stone: side effect inside expression: call to 'add_and_return' modifies its arguments
int effect_in_expr(int a) { return add_and_return(a) + a; }

// rules: STN-21
// expect: not-stone: side effect inside expression: call to 'Account::deposit' modifies its arguments
int method_effect_in_expr(Account a) { return (a.deposit(1), a.balance); }

// rules: STN-21
// expect: not-stone: side effect inside expression: call to 'inc' modifies its arguments
int effect_in_argument(int a) { return twice_inc((inc(a), a)); }

// Hela villkoret i en if får vara ett anrop med sidoeffekt: det utvärderas en
// gång, före grenarna, som en sats (rugby: `if (run_towards(player, target))`).
// rules: STN-12
// expect: stone
bool step_one(int& x) { x = x + 1; return x > 3; }
// rules: STN-21
// expect: stone
int effect_in_condition(int x) { if (step_one(x)) return 1; return x; }
// rules: STN-21
// expect: stone
int effect_in_condition_else(int x) { if (step_one(x)) return x; else return 0; }

// Bara det yttersta ledet: inte i &&, !, jämförelser eller loopvillkor.
// rules: STN-21
// expect: not-stone: side effect inside expression: call to 'step_one' modifies its arguments
int effect_in_and(int x) { if (step_one(x) && x > 5) return 1; return x; }
// rules: STN-21
// expect: not-stone: side effect inside expression: call to 'step_one' modifies its arguments
int effect_in_not(int x) { if (!step_one(x)) return 1; return x; }
// rules: STN-21
// expect: not-stone: side effect inside expression: call to 'step_one' modifies its arguments
int effect_in_while(int x) { while (step_one(x)) {} return x; }

// --- Fortfarande underkända ---

// rules: STN-9, STN-10
// expect: not-stone: return type int &
int& returns_ref(int& a) { return a; }

// rules: STN-12
// expect: not-stone: parameter 'v' has type Vec2 &&
int rvalue_ref(Vec2&& v) { return v.x; }

// --- *this som argument -------------------------------------------------------

struct Box;
int peek(const Box& b);
void poke(Box& b);
void poke_with(Box& b, const int& v);

struct Box {
  int v;
  // rules: STN-7, STN-26
  // expect: stone
  int read() const { return peek(*this); }
  // rules: STN-7, STN-26
  // expect: stone
  void write() { poke(*this); }
  // rules: STN-62
  // expect: not-stone: may be the same object
  void write_alias() { poke_with(*this, v); }
};

// rules: STN-12
// expect: stone
int peek(const Box& b) { return b.v; }
// rules: STN-12
// expect: stone
void poke(Box& b) { b.v = 1; }
// rules: STN-12
// expect: stone
void poke_with(Box& b, const int& v) { b.v = v; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"alias_target", [] { return alias_target(1) == 2 ? 0 : 1; }},
               {"not_a_path", [] { return not_a_path(1) == 2 && not_a_path(-1) == 0 ? 0 : 1; }})
#endif
