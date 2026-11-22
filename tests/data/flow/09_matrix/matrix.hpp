// Testmatris (docs/bronze.md, steg 2): samma läsning och skrivning av ett
// element nås på olika sätt. Varje form ska ge samma beroenden, och
// beroendeoraklet (--flow-oracle) försöker motbevisa dem. PORK_PRE är ett
// antagande, inget beroende (som i SPARK).
#pragma once
#include <pork/annotations.hpp>
#include <optional>
#include <vector>

struct Inner { int b = 0; };
struct Outer { Inner a; int c = 0; };
struct Item {
  int v = 0;
  // flow: return <- this.v
  // flow oracle: ok
  int get() const { return v; }
  // flow: return <- this.v
  // flow oracle: ok
  int get_mut() { return v; }
  // flow: this.v <- x (always written)
  // flow oracle: ok
  void set(int x) { v = x; }
};

// I en annan översättningsenhet (other.cpp).
int read_other(const int& r);
void write_other(int& r, int x);
