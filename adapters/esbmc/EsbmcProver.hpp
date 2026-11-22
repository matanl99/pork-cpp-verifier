#pragma once

#include "Ports.hpp"

#include <llvm/ADT/StringRef.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace pork {

// Bevisare: kör ESBMC som egen process på samma C-program som CbmcProver
// (plus kontraktsfilerna) och läser resultattabellen i textutdatan.
//
// Programmet använder CBMC:s inbyggda funktioner (__CPROVER_assert m.fl.).
// En liten header (kCompat i .cpp) mappar dem till ESBMC:s och tas med via
// --include-file. ESBMC behandlar en funktion utan kropp som en varning och
// ett godtyckligt returvärde; här blir den ett fel (missingFunction), annars
// skulle t.ex. en omappad __CPROVER_assert ge ett tomt bevis.
//
// Kontroller: signed overflow och flyttals-overflow (--overflow-check), NaN,
// odefinierade skift, och ESBMC:s standardkontroller (division med noll,
// pekare, arraygränser, unwinding assertions).
//
// SMT-lösaren länkas in i ESBMC (bitwuzla som standard, även z3 m.fl. om
// ESBMC är byggd med dem).
class EsbmcProver : public IProver {
 public:
  struct Options {
    std::string esbmc = "esbmc";          // programnamn eller sökväg
    std::vector<std::string> contracts;   // C-filer med kontrakt för betrodda funktioner
    std::vector<std::string> includeDirs;  // -I för kontrakten (pork_contracts.h)
    unsigned unwind = 64;
    bool strict = false;                  // --silver-strict: även unsigned-omslag
    unsigned timeoutSeconds = 120;
    unsigned memoryLimitMB = 4096;        // per esbmc-process, 0 = ingen gräns
    std::string keepDir;                  // spara genererad C här (felsökning), "" = nej
    std::string solver = "bitwuzla";      // --bitwuzla, --z3, ...
  };

  explicit EsbmcProver(Options options);

  // Lösare som ESBMC kan ha inbyggda.
  static bool knownSolver(const std::string& solver);

  // Tolkar esbmc:s utdata (stdout+stderr) och slutkod. Rent, för enhetstest.
  static ProofResult parseOutput(llvm::StringRef output, int exitCode, unsigned unwind, unsigned memoryLimitMB,
                                 bool strict = false);

  // ESBMC:s beskrivning av en egenskap i pork:s (CBMC:s) ordalydelse, så att
  // korpusen gäller för båda bevisarna. ESBMC skiljer inte på signed och
  // unsigned overflow; med `strict` (även unsigned kontrolleras) heter det "integer".
  static std::string normalize(llvm::StringRef description, unsigned unwind, bool strict = false);

  ProofResult prove(const std::string& program) override;
  std::string configuration() const override;
  unsigned timeoutSeconds() const override { return options_.timeoutSeconds; }

 private:
  Options options_;
  std::atomic<unsigned> count_ = 0;  // --keep-c-filer; prove() körs från flera trådar
  mutable std::once_flag configured_;
  mutable std::string configuration_;
};

}  // namespace pork
