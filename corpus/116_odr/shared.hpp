// Funktioner definierade i en header som två översättningsenheter inkluderar
// (STN-94). Samma definition i båda: en dom per funktion. Olika definitioner
// (ett makro före #include) bryter mot ODR ([basic.def.odr]): länkaren väljer
// en av dem, så ingen får ge domen.
#pragma once

extern int counter;

// rules: STN-94
// expect: stone
inline int twice(int x) { return 2 * x; }

struct Box {
  int n;
  // rules: STN-94
  // expect: stone
  int doubled() const { return twice(n); }
};

template <typename T>
// rules: STN-94
// expect: pick<int>: stone
// expect: pick<long>: stone
T pick(T a, T b) { return a < b ? a : b; }

#ifdef USE_COUNTER
inline int base() { return counter; }
#else
// rules: STN-94
// expect: not-stone: defined differently in the translation units
inline int base() { return 1; }
#endif
