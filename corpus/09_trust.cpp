// Betrodda funktioner (axiom): trust-fil (09_trust.trust) eller
// [[clang::annotate("pork::trusted")]]. Anropare blir "stone: assuming ...".

#include <cmath>

int g = 0;

double ext_scale(double x);    // i trust-filen
double ext_unknown(double x);  // inte betrodd
[[clang::annotate("pork::trusted")]] double ext_annotated(double x);

// rules: STN-24
// expect: stone: assuming std::fabs
float abs_f(float x) { return std::fabs(x); }

// rules: STN-24
// expect: stone: assuming hypot
double len(double x, double y) { return ::hypot(x, y); }

// rules: STN-24
// expect: stone: assuming ext_scale
double scaled(double x) { return ext_scale(x); }

// rules: STN-23, STN-24
// expect: not-stone: calls 'ext_unknown' with no definition in the analysed sources
double unknown(double x) { return ext_unknown(x); }

// rules: STN-24
// expect: stone: assuming ext_annotated
double annotated_decl(double x) { return ext_annotated(x); }

// rules: STN-24
// expect: trusted
[[clang::annotate("pork::trusted")]] int reads_global_trusted(int a) { return a + g; }

// rules: STN-24
// expect: stone: assuming reads_global_trusted
int uses_trusted(int a) { return reads_global_trusted(a); }

// Antaganden följer med transitivt, sorterade.
// rules: STN-24
// expect: stone: assuming ext_scale, hypot, std::fabs
double combined(double x) { return scaled(x) + len(x, x) + abs_f(float(x)); }

// rules: STN-23
// expect: not-stone: calls non-stone 'unknown'
double via_unknown(double x) { return unknown(x); }

// Står i trust-filen men har en definition: analysen gäller.
// rules: STN-24
// expect: not-stone: uses variable 'g'
int local_impure(int a) { return a + g; }

// rules: STN-23, STN-24
// expect: not-stone: calls non-stone 'local_impure'
int calls_local_impure(int a) { return local_impure(a); }
