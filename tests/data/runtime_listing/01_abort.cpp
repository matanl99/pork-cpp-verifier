// Självtest (granskning 1, P2): programmet kraschar när fallen listas. Det
// ska fälla testet, inte ge noll körningar och grönt.
#include <cstdlib>
int f(int x) { return x; }
#ifdef PORK_RUN
int main() { std::abort(); }
#endif
