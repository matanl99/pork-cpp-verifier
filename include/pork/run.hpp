#pragma once

// Körbara fall för körningsoraklet (tests/check_runtime.py, P-e i
// docs/stone_model.md). Ett korpusfall lägger sina körningar sist, inom
// #ifdef PORK_RUN, så att pork aldrig ser dem:
//
//   #ifdef PORK_RUN
//   #include <pork/run.hpp>
//   PORK_RUN_CASES(
//       {"sort_rows", [] { Rows m{{3, 1}, {2}}; sort_rows(m); return 0; }},
//       {"bad_sort_two_rows", [] { Rows m{{3, 1}, {2}}; bad_sort_two_rows(m); return 0; }})
//   #endif
//
// Namnet är funktionen som körningen prövar, som pork skriver den
// (`Outer::Outer`; `namn@rad` när flera funktioner har samma namn, raden
// där pork rapporterar funktionen; `namn#2` för en andra körning). En
// körning som larmar (ASan, UBSan, MSan, _GLIBCXX_DEBUG) eller returnerar
// något annat än 0, för en funktion som pork säger är Stone, är ett hål.
// Returvärdet kan visa ett brott som sanitizers inte ser (G4: en global
// som ändrades).

#include <cstdio>
#include <cstring>

namespace pork_run {
struct Case {
  const char* name;
  int (*run)();
};
}  // namespace pork_run

#ifdef PORK_RUN_AS_CONSTEXPR
// tests/check_constexpr.py: körningarna som constexpr-prov. Oraklet lägger
// till ett static_assert(cases[k].run() == 0) per körning sist i en kopia.
#define PORK_RUN_CASES(...) \
  namespace pork_run { constexpr Case cases[] = {__VA_ARGS__}; }
#else
#define PORK_RUN_CASES(...)                                                 \
  int main(int argc, char** argv) {                                         \
    static const ::pork_run::Case cases[] = {__VA_ARGS__};                  \
    for (const ::pork_run::Case& c : cases) {                               \
      if (argc < 2) std::puts(c.name);                                      \
      else if (std::strcmp(argv[1], c.name) == 0) return c.run();           \
    }                                                                       \
    return argc < 2 ? 0 : 99;                                               \
  }
#endif
