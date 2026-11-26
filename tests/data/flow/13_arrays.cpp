// Ägda arrayer (STN-75) i --flow. Silver översätter dem (SLV-44), så
// oraklet prövar flödet; elementen heter m[*]. Förvillkoren gör fallen
// silver, så att oraklet prövar dem.

#include <pork/annotations.hpp>

// Range-for med skrivning: kompilatorns __begin är en rå pekare, och
// `++__begin` skriver bara den. Loopvillkoret läser m (pekarna in i m).
// flow: m[*] <- c, m, m[*] (maybe written)
void fill(int (&m)[2], int c) {
  for (int& cell : m) cell = c;
}

// flow: return <- m
long long sum(const int (&m)[3]) {
  long long s = 0;
  for (int x : m) s += x;
  return s;
}

// En statisk lokal konstant tabell: läsningen beror bara på indexet.
// flow: return <- i
int table(int i) {
  PORK_PRE(i >= 0 && i < 2);
  static constexpr int values[2] = {1, 2};
  return values[i];
}

// flow: return <- i
int const_table(int i) {
  PORK_PRE(i >= 0 && i < 3);
  static const int values[3] = {4, 5, 6};
  return values[i];
}
