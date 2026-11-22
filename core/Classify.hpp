#pragma once

#include "Facts.hpp"

#include <map>
#include <string>

namespace pork {

// usr -> slutligt besked.
//
// En funktion märkt pork::trusted får beskedet trusted; dess kropp och anrop
// spelar ingen roll. Övriga är Stone om de lokalt följer delmängden och varje
// anropad funktion antingen
//   - är definierad bland de analyserade källorna och själv Stone eller trusted, eller
//   - saknar definition men är betrodd (pork::trusted på deklarationen, eller
//     namnet står i trust-listan).
// Trust-listan gäller alltså bara funktioner utan definition: analys går före lista.
//
// Beräknas som största fixpunkt: börja med alla lokalt godkända som Stone och
// degradera tills inget ändras. Ömsesidig rekursion mellan godkända
// funktioner förblir alltså Stone (terminering ingår inte i Stone).
//
// Därefter samlas antaganden: för varje Stone-funktion de betrodda typer och
// funktioner den vilar på, direkt eller via andra Stone-funktioner. En
// betrodd funktion utan definition redovisas med sitt trust-namn (utan
// templateargument), en betrodd definition med sitt fulla namn.
std::map<std::string, Verdict> classify(const FactsMap& facts, const TrustList& trust);

}  // namespace pork
