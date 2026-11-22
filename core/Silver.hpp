#pragma once

#include "Facts.hpp"
#include "Flow.hpp"
#include "Ports.hpp"

#include <functional>
#include <map>
#include <string>

namespace pork {

// Silver: frånvaro av körtidsfel. För varje Stone-funktion sätts ett C-program
// ihop av dess översättning och översättningarna av allt den anropar
// (transitivt; betrodda anrop går till kontrakt), och bevisaren prövar det.
// Bevisat => kind = silver. Annars förblir den stone med orsaken i `reason`.
// Upp till `jobs` program prövas samtidigt; resultatet beror inte på jobs.
// Med `wanted` prövas bara de funktioner den godtar (anropade funktioner följer
// ändå med i beviset); övriga förblir stone utan orsak (ej prövade).
void silver(const FactsMap& facts, std::map<std::string, Verdict>& verdicts, IProver& prover,
            unsigned jobs = 1, const std::function<bool(const FunctionFacts&)>& wanted = {});

// --flow-oracle (docs/bronze.md, steg 3): försöker motbevisa det härledda
// flödet för Silver-funktioner. För varje del av en utdata körs funktionen två
// gånger i samma C-program, med lika värden i det flödet säger att delen beror
// på och godtyckliga värden i resten; bevisaren visar att delen blir lika.
// Ett motexempel är ett hål i --flow. Resultaten hamnar i Verdict::flowOracle.
// selfTest: varje utdata tappar ett beroende innan provet (då ska hål synas).
void flowOracle(const FactsMap& facts, std::map<std::string, Verdict>& verdicts,
                const std::map<std::string, FlowSummary>& flows, IProver& prover, unsigned jobs, bool selfTest = false);

}  // namespace pork
