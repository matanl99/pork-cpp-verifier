#pragma once

#include "Facts.hpp"

#include <expected>
#include <string>
#include <vector>

namespace pork {

// Inkommande port: varifrån fakta om funktionsdefinitioner kommer.
// Adapter i produktion: clang/LibTooling. I test: en fejk med handskrivna fakta.
class IFactsSource {
 public:
  virtual ~IFactsSource() = default;
  // Fel (t.ex. kompileringsfel) => inga fakta alls. Ett halvt AST ger inga garantier.
  virtual std::expected<FactsMap, std::string> collect() = 0;
  // Efter collect(): krav utanför funktionskroppar (BRZ-6), sorterade. Samma
  // förekomst (occurrence) kan stå flera gånger med olika visningsläge.
  virtual std::vector<StrayRequirement> strayRequirements() const { return {}; }
};

// Ett körtidsfel (eller saknat kontrakt) som bevisaren hittade.
struct ProofFailure {
  std::string description;      // "arithmetic overflow on signed + in a + b"
  std::string file;             // i C++-källan (översättningen bär #line)
  unsigned line = 0;
  std::string missingFunction;  // C-namn på en funktion utan kropp (= kontrakt saknas)

  std::string modelInputs{};

  bool operator==(const ProofFailure&) const = default;
};

// Bevisaren kunde inte svara. En timeout är ett svar om programmet ("för
// svårt med den här tidsgränsen") och får sparas; övriga fel (cbmc saknas,
// kraschade) gäller körningen.
struct ProverError {
  std::string message;
  bool timedOut = false;

  bool operator==(const ProverError&) const = default;
};

using ProofResult = std::expected<std::vector<ProofFailure>, ProverError>;

// Utgående port: bevisare för Silver. Får ett C-program med ingången
// pork_harness() och svarar med de egenskaper som inte gick att bevisa
// (tomt = bevisat). prove() anropas från flera trådar samtidigt (Silver kör
// funktionerna parallellt).
class IProver {
 public:
  virtual ~IProver() = default;
  virtual ProofResult prove(const std::string& program) = 0;

  // Allt utöver programmet som påverkar svaret (version, flaggor, kontrakt).
  // Samma program och konfiguration => samma svar (se CachingProver).
  // Tidsgränsen ingår inte: den avgör bara om ett svar hinner komma.
  virtual std::string configuration() const = 0;

  // Tidsgränsen per bevis i sekunder, 0 = ingen. CachingProver återanvänder en
  // sparad timeout bara om den nya gränsen inte är större.
  virtual unsigned timeoutSeconds() const { return 0; }
};

// Utgående port: vart beskeden tar vägen.
class IReport {
 public:
  virtual ~IReport() = default;
  // Anropas en gång per funktion, i ordningen fil, rad, kolumn, namn.
  virtual void function(const FunctionFacts& f, const Verdict& v) = 0;
  // Ett krav som inte håller: märkt pork::stone men inte Stone, eller ett
  // PORK_DEPENDS-krav som inte håller (BRZ-6). Anropas efter alla function().
  virtual void violation(const FunctionFacts& f, const Verdict& v) = 0;
  // Ett PORK_DEPENDS utanför varje funktionskropp (BRZ-6). Efter violation().
  virtual void strayRequirement(const StrayRequirement& s) = 0;
  virtual void error(const std::string& message) = 0;
};

}  // namespace pork
