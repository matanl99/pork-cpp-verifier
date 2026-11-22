// Ägda arrayer (STN-75) i --flow. Silver översätter
// inte arrayer än, så oraklet prövar dem inte.

// Range-for med skrivning: kompilatorns __begin är en rå pekare, och
// `++__begin` skriver bara den. Loopvillkoret läser m (pekarna in i m).
// flow: m[*] <- c, m, m[*] (maybe written)
void fill(int (&m)[2], int c) {
  for (int& cell : m) cell = c;
}

// flow: return <- m
int sum(const int (&m)[3]) {
  int s = 0;
  for (int x : m) s += x;
  return s;
}

// En statisk lokal konstant tabell: läsningen beror bara på indexet.
// flow: return <- i
int table(int i) {
  static constexpr int values[2] = {1, 2};
  return values[i];
}

// flow: return <- i
int const_table(int i) {
  static const int values[3] = {4, 5, 6};
  return values[i];
}
