// BRZ-7: satser och villkor vars värde inte når någon utdata.
#include <stdexcept>

void sink(int x);  // ingen kropp: okänd kod (verkan utanför)

// Avsiktligt skrivfel: den tomma satsen är hela if-kroppen.
// flow: return <- count
// flow: no effect: the condition at line 10
unsigned count_vote(unsigned count, bool valid) {
  if (valid);
  {
    ++count;
  }
  return count;
}

// flow: return <- count, valid
unsigned count_vote_control(unsigned count, bool valid) {
  if (valid) ++count;
  return count;
}

struct Answer {
  bool round_open;
  bool correct;
};

// flow: return <- answer.round_open
bool round_is_open(const Answer& answer) { return answer.round_open; }

// flow: return <- answer.correct
// flow: no effect: the call of 'round_is_open' at line 34
bool discarded_check(const Answer& answer) {
  static_cast<void>(round_is_open(answer));
  return answer.correct;
}

// flow: return <- answer, answer.correct
bool both_checks(const Answer& answer) {
  if (!round_is_open(answer)) return false;
  return answer.correct;
}

// flow: x <- x (always written)
void bump(int& x) { ++x; }

// En lokal som ingen läser: anropet ändrar den förgäves.
// flow: return <- a
// flow: no effect: the initialisation of 'local' at line 52
// flow: no effect: the call of 'bump' at line 53
int bump_local(int a) {
  int local = a;
  bump(local);
  return a;
}

// flow: out <- out (always written)
void bump_out(int& out) { bump(out); }

// flow: return <- b
// flow: no effect: the initialisation of 'x' at line 64
// flow: no effect: the assignment to 'x' at line 65
int dead_store(int a, int b) {
  int x = 0;
  x = a;
  x = b;
  return x;
}

// flow: return <- nothing
// flow: no effect: the assignment to 'i' at line 75
// flow: no effect: the condition at line 75
// flow: no effect: the initialisation of 'i' at line 75
int empty_loop(int n) {
  for (int i = 0; i < n; ++i) {
  }
  return 0;
}

// Okänd kod har verkan: argumentet och villkoret som leder dit räknas.
// flow: return <- nothing
int to_sink(int x) {
  if (x > 0) sink(x);
  return 0;
}

// Ett kast är en verkan: villkoret räknas, och utdata vid kastet syns för en
// anropare som fångar.
// flow: return <- x
int check(int x) {
  if (x < 0) throw std::invalid_argument("negative");
  return 0;
}

// flow: return <- x
int checked(int x) {
  check(x);
  return x;
}

// Samma anrop utan kast i den anropade: utan verkan.
// flow: return <- x
int quiet(int x) { return x - x + 0 * x; }

// flow: return <- x
// flow: no effect: the call of 'quiet' at line 108
int quiet_call(int x) {
  quiet(x);
  return x;
}

// flow: return <- nothing
int written_before_throw(int x, int& out) {
  out = x;
  throw std::runtime_error("always");
}
