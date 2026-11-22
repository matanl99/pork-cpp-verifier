#pragma once

// Provanrop för constexpr-oraklet (tests/check_constexpr.py). Ett korpusfall
// lägger sina prov sist, inom #ifdef PORK_CONSTEXPR, så att pork aldrig ser
// dem:
//
//   #ifdef PORK_CONSTEXPR
//   #include <pork/constexpr.hpp>
//   PORK_CONSTEXPR_CASE("sort_rows", std::vector<int> v{3, 1}; sort_rows(v); return v[0] == 1;)
//   #endif
//
// Namnet är funktionen som provet prövar, som i PORK_RUN_CASES (`namn@rad`,
// `namn#2`). Kroppen returnerar true. Oraklet sätter `constexpr` på
// funktionerna i en kopia av filen; kompilatorn måste då utvärdera provet
// och avvisa odefinierat beteende i kärnspråket ([expr.const]/5), också
// det som ASan inte ser (råa pekare i olika objekt, läsning efter livstidens
// slut på stacken). Ett prov per rad där makrot börjar.

#define PORK_CONSTEXPR_CASE(name, ...) static_assert([]() constexpr -> bool { __VA_ARGS__ }(), name);
