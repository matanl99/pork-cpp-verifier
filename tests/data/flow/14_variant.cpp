// Indata som union (std::variant) i beroendeoraklet, som rugbys player_of:
// körning a och b antas lika och en unionmedlem kopieras. Harnessens indata
// kommer från nondet-funktioner; flow_oracle prövar att svaret är ok.
#include <variant>

struct RunTo {
  int player;
  int x;
};
struct PassTo {
  int player;
  int target;
};
using Command = std::variant<RunTo, PassTo>;

// flow: return <- c
// flow oracle: ok
int player_of(const Command& c) {
  return std::visit([](const auto& cmd) { return cmd.player; }, c);
}
