// Ett kast (STN-83) är ingen normal retur: vägen räknas inte vid utgången.
#include <stdexcept>

// Bara den normala vägen når retur. x styr om funktionen returnerar och
// räknas med (en överskattning: sunt, inte exakt som SPARK).
// flow: out <- a, x (always written)
void write_after_check(int x, int a, int& out) {
  if (x < 0) throw std::length_error("negative");
  out = a;
}

// Skrivningen före kastet syns bara på den normala vägen, där den alltid sker.
// flow: return <- x
// flow: out <- x (always written)
int write_before(int x, int& out) {
  out = x;
  if (x > 9) throw std::out_of_range("too big");
  return x;
}

// Ingen normal retur alls.
// flow: return <- nothing
int never_returns(int x, int& out) {
  out = x;
  throw std::runtime_error("always");
}
