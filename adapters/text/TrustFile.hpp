#pragma once

#include "Facts.hpp"

#include <expected>
#include <istream>
#include <string>

namespace pork {

// Trust-fil: ett kvalificerat funktionsnamn (utan templateargument) per rad,
// t.ex. `std::fabs` eller `hypot`. Tomma rader och `#`-kommentarer ignoreras,
// liksom blanktecken runt namnet.
//
//   std::fabs              funktion
//   std::optional::*       alla medlemmar i std::optional
//   type std::optional     std::optional<T> är en värdetyp när T är det
TrustList parseTrustList(std::istream& in);

std::expected<TrustList, std::string> loadTrustList(const std::string& path);

}  // namespace pork
