// BRZ-7: en sats eller ett villkor utan verkan gör att en funktion med krav
// inte är Bronze. PORK_DEPENDS är en övre gräns (vad utdata får bero på); en
// funktion som beror på för lite uppfyller den alltid. Felen nedan är
// väldefinierade program (Stone), men logiken är fel.

#include <pork/annotations.hpp>

// Den tomma satsen är hela if-kroppen: rösten räknas alltid.
// rules: BRZ-7, STN-13
// expect: stone: not bronze: the condition at line 16 has no effect
// expect-violation
unsigned count_vote(unsigned count, bool valid) {
  PORK_DEPENDS_RESULT(count, valid);
  // clang-format off
  // expect-warning: the condition has no effect
  if (valid);
  // clang-format on
  {
    ++count;
  }
  return count;
}

// rules: BRZ-7
// expect: bronze
unsigned count_vote_control(unsigned count, bool valid) {
  PORK_DEPENDS_RESULT(count, valid);
  if (valid) ++count;
  return count;
}

struct Answer {
  bool round_open;
  bool correct;
};

// rules: BRZ-7
// expect: bronze
[[nodiscard]] bool round_is_open(const Answer& answer) {
  PORK_DEPENDS_RESULT(answer.round_open);
  return answer.round_open;
}

// Kontrollen kastas med en cast till void: poäng också när omgången är stängd.
// rules: BRZ-7, STN-19
// expect: stone: not bronze: the call of 'round_is_open' at line 51 has no effect
// expect-violation
bool discarded_check(const Answer& answer) {
  PORK_DEPENDS_RESULT(answer);
  // expect-warning: the call of 'round_is_open' has no effect
  static_cast<void>(round_is_open(answer));
  return answer.correct;
}

// rules: BRZ-7
// expect: bronze
bool both_checks(const Answer& answer) {
  PORK_DEPENDS_RESULT(answer);
  if (!round_is_open(answer)) return false;
  return answer.correct;
}

// En död tilldelning.
// rules: BRZ-7
// expect: stone: not bronze: the initialisation of 'total' at line 70 has no effect (+1 more)
// expect-violation
int dead_store(int a, int b) {
  PORK_DEPENDS_RESULT(a, b);
  // expect-warning: the initialisation of 'total' has no effect
  int total = 0;
  // expect-warning: the assignment to 'total' has no effect
  total = a;
  total = b;
  return total;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"count_vote", [] { return count_vote(10, false) == 11 && count_vote(10, true) == 11 ? 0 : 1; }},
    {"count_vote_control", [] { return count_vote_control(10, false) == 10 && count_vote_control(10, true) == 11 ? 0 : 1; }},
    {"discarded_check", [] { return discarded_check(Answer{false, true}) ? 0 : 1; }},
    {"both_checks", [] { return !both_checks(Answer{false, true}) && both_checks(Answer{true, true}) ? 0 : 1; }},
    {"dead_store", [] { return dead_store(1, 2) == 2 ? 0 : 1; }})
#endif
