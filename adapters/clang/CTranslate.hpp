#pragma once

#include "Facts.hpp"

#include <clang/AST/Decl.h>

#include <expected>
#include <string>

namespace pork {

// Översätter en (lokalt Stone-godkänd) funktion till C för CBMC.
//
// Delmängden är Stones: skalärer, enums (som heltal), värdestructar, metoder
// (this som värde eller pekare), `const T&` (som värde: Stone garanterar att
// inget ändras under anropet), `T&` (som pekare), lokala referenser bundna
// till en väg (som pekare) eller en temporär (som värde).
//
//  - Varje sats får `#line` så att CBMC:s fynd pekar in i C++-källan.
//  - PORK_PRE blir __CPROVER_assert i `definition` och __CPROVER_assume i `entry`.
//  - Anrop till betrodda funktioner (pork::trusted, systemheaders, utan
//    definition) går till ett kontrakt: std::hypot(float, float) ->
//    std__hypot__float__float.
//  - `entry` innehåller också pork_harness(): godtyckliga argument (flyttal
//    ändliga, enums uppräknade värden) och ett anrop.
//
// `std::vector<T>` blir { size, data[vectorBound] }: beviset gäller bara vektorer
// med högst så många element, vilket redovisas som ett antagande.
//
// Det som inte stöds ger ett fel med orsak, aldrig en tyst approximation.
struct TranslateOptions {
  unsigned vectorBound = 16;
  // --silver-strict: heltalskonverteringar där värdet inte ryms blir en assert.
  bool strict = false;
  bool counterexample = false;
};

std::expected<CFunction, std::string> translateToC(const clang::FunctionDecl& fd,
                                                   const TranslateOptions& options = {});

}  // namespace pork
