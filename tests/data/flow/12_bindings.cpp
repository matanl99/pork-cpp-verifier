// Structured bindings (STN-84) i --flow: en bindning är clangs
// bindningsuttryck, ett fält i den dolda variabeln, så `return a + b` beror
// på fälten. Silver översätter inte bindningar än, så oraklet prövar dem inte.
#include <utility>

struct P {
  int x;
  int y;
};

// flow: return <- p
int ok_copy(P p) {
  auto [a, b] = p;
  return a + b;
}

// Bindningen till en kopia: skrivningen ändrar inte p.
// flow: return <- p, p.x
int ok_copy_write(P p) {
  auto [a, b] = p;
  a = 5;
  return a + b + p.x;
}

// flow: p.x <- p.y (always written)
void ok_ref_write(P& p) {
  auto& [a, b] = p;
  a = b;
}

// flow: return <- p.x, p.y
int ok_const_ref(const P& p) {
  const auto& [a, b] = p;
  return a * b;
}

// Bara den ena bindningen läses.
// flow: return <- p.y
int only_second(const P& p) {
  const auto& [a, b] = p;
  return b;
}

// std::pair via std::get: hållaren är ett okänt element av den dolda
// variabeln, som i Stone.
// flow: return <- p[*]
int pair_first(const std::pair<int, int>& p) {
  const auto& [first, second] = p;
  return first;
}

// flow: p[*] <- c, p[*] (maybe written)
void pair_write(std::pair<int, int>& p, int c) {
  auto& [first, second] = p;
  first = c;
}
