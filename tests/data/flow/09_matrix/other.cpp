#include "matrix.hpp"

// flow: return <- r
// flow oracle: ok
int read_other(const int& r) { return r; }
// flow: r <- x (always written)
// flow oracle: ok
void write_other(int& r, int x) { r = x; }
