// Självtest för tests/check_constexpr.py: ett PORK_CONSTEXPR_CASE som inte går
// att utvärdera (läser en global) prövar inget och fäller testet.
int counter = 0;

// expect: not-stone
int read_counter() { return counter; }

#ifdef PORK_CONSTEXPR
#include <pork/constexpr.hpp>
PORK_CONSTEXPR_CASE("read_counter", return read_counter() == 0;)
#endif
