// Självtest (granskning 1, P1): två överlagringar med samma namn. Körningen
// "target" är tvetydig och ska fälla testet, inte slås ihop med den underkända.
int g = 0;
int target(int x) { return x - 1; }
double target(double d) { g = 1; return d; }
#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES({"target", [] { return target(2); }})
#endif
