#pragma once

#include "Ports.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pork {

// Bevisare: kör CBMC som egen process på det genererade C-programmet plus
// kontraktsfilerna, och läser resultatet (--json-ui).
//
// Kontroller: signed overflow, division med noll (heltal och flyttal),
// odefinierade skift, flyttals-overflow och NaN, alla assert i programmet
// (förvillkor, flyttal -> heltal), deref av null och att loopar tar slut
// inom `unwind` varv.
//
// prove() får anropas från flera trådar. Tidsgränsen hålls genom polling
// (inte SIGALRM, som delas av hela processen), och varje cbmc-process får
// en minnesgräns (RLIMIT_AS).
class CbmcProver : public IProver {
 public:
  struct Options {
    std::string cbmc = "cbmc";            // programnamn eller sökväg
    std::vector<std::string> contracts;   // C-filer med kontrakt för betrodda funktioner
    std::vector<std::string> includeDirs;  // -I för kontrakten (pork_contracts.h)
    unsigned unwind = 64;
    bool strict = false;                  // --silver-strict: även unsigned-omslag
    unsigned timeoutSeconds = 120;
    unsigned memoryLimitMB = 4096;        // per cbmc-process, 0 = ingen gräns
    std::string keepDir;                  // spara genererad C här (felsökning), "" = nej
    // minisat (inbyggd), en SMT-lösare cbmc känner (z3, cvc5, bitwuzla, ...)
    // eller en extern SAT-lösare som läser DIMACS (cadical, programnamn/sökväg).
    std::string solver = "minisat";
    bool counterexample = false;
    // --no-simplify: reservväg för program med unioner (UnionRoutingProver).
    bool noSimplify = false;
  };

  explicit CbmcProver(Options options);

  // cbmc-flaggorna för en lösare.
  static std::vector<std::string> solverFlags(const std::string& solver);

  ProofResult prove(const std::string& program) override;
  std::string configuration() const override;
  unsigned timeoutSeconds() const override { return options_.timeoutSeconds; }

 private:
  Options options_;
  std::vector<std::string> solverFlags_;
  std::atomic<unsigned> count_ = 0;  // --keep-c-filer; prove() körs från flera trådar
  mutable std::once_flag configured_;
  mutable std::string configuration_;  // cbmc --version + inställningar, beräknas en gång
};

}  // namespace pork
