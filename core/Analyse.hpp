#pragma once

#include "Ports.hpp"

#include <string>
#include <vector>

namespace pork {

// Exit-koder, som grep/diff: 0 = inga fynd, 1 = fynd, 2 = verktyget kunde inte köra.
enum ExitCode : int {
  ok = 0,
  requirementViolated = 1,  // minst en funktion märkt pork::stone blev inte Stone
  analysisFailed = 2,       // t.ex. kompileringsfel; felet går till report.error
};

// Användningsfallet: hämta fakta, klassa, pröva Stone-funktioner för Silver
// (om en bevisare ges), rapportera i deterministisk ordning och sist
// rapportera brutna pork::stone-krav.
//
// `only`: kataloger (absoluta). Om den inte är tom rapporteras, prövas för
// Silver och kontrolleras (pork::stone) bara funktioner i filer under någon av
// dem. Klassningen sker ändå över alla funktioner: Stone beror på anropade
// funktioner, även i headers utanför.
// --flow-oracle (testorakel för --flow, kräver en bevisare).
enum class OracleMode { off, on, selfTest };

int analyse(IFactsSource& source, const TrustList& trust, IReport& report,
            IProver* prover = nullptr, unsigned jobs = 1, const std::vector<std::string>& only = {},
            OracleMode oracle = OracleMode::off, bool showFlow = false, bool warnings = true);

// Ligger `file` under någon av katalogerna (lexikalt, efter normalisering)?
bool underAny(const std::string& file, const std::vector<std::string>& dirs);

}  // namespace pork
