#pragma once

#include "CTranslate.hpp"
#include "LoanCheck.hpp"
#include "Ports.hpp"

#include <clang/Tooling/CompilationDatabase.h>

#include <string>
#include <vector>

namespace pork {

// Inkommande adapter: kör clang (LibTooling) över källfilerna och samlar
// FunctionFacts för varje funktionsdefinition utanför systemheaders.
class ClangFactsSource : public IFactsSource {
 public:
  // `trust.types` behövs redan här: om en typ är en värdetyp avgörs i adaptern.
  // `translate`: översätt lokalt godkända funktioner till C (för Silver).
  ClangFactsSource(const clang::tooling::CompilationDatabase& db,
                   std::vector<std::string> files, std::string resourceDir, TrustList trust,
                   std::optional<TranslateOptions> translate = std::nullopt, LoanOptions loans = {},
                   unsigned jobs = 1, bool flow = false)
      : db_(db), files_(std::move(files)), resourceDir_(std::move(resourceDir)),
        trust_(std::move(trust)), translate_(translate), loans_(std::move(loans)), jobs_(jobs), flow_(flow) {}

  std::expected<FactsMap, std::string> collect() override;
  std::vector<StrayRequirement> strayRequirements() const override { return stray_; }

 private:
  bool runOne(const std::string& file, FactsMap& facts, std::vector<StrayRequirement>& stray) const;

  const clang::tooling::CompilationDatabase& db_;
  std::vector<std::string> files_;
  std::string resourceDir_;
  TrustList trust_;
  std::optional<TranslateOptions> translate_;
  LoanOptions loans_;
  unsigned jobs_;  // översättningsenheter som analyseras samtidigt
  bool flow_;      // --flow: bygg flödesgrafer
  std::vector<StrayRequirement> stray_;  // collect(): krav utanför funktionskroppar
};

}  // namespace pork
