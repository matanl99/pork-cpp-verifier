#include "Analyse.hpp"

#include "Classify.hpp"
#include "Silver.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <tuple>
#include <vector>

namespace pork {

bool underAny(const std::string& file, const std::vector<std::string>& dirs) {
  const std::filesystem::path f = std::filesystem::path(file).lexically_normal();
  return std::ranges::any_of(dirs, [&](const std::string& d) {
    const std::filesystem::path rel = f.lexically_relative(std::filesystem::path(d).lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
  });
}

int analyse(IFactsSource& source, const TrustList& trust, IReport& report, IProver* prover,
            unsigned jobs, const std::vector<std::string>& only, OracleMode oracle, bool showFlow) {
  auto facts = source.collect();
  if (!facts) {
    report.error(facts.error());
    return analysisFailed;
  }

  auto verdict = classify(*facts, trust);
  auto inScope = [&](const FunctionFacts& f) { return only.empty() || underAny(f.file, only); };

  // --flow: flödet för Stone-funktioner (docs/bronze.md, steg 1). Bara Stone:
  // analysen bygger på att det inte finns dolda alias (G3) eller globaler.
  // Bronze (spec/3_bronze.txt): flödeskraven mot det härledda flödet. Bara
  // när någon funktion har krav, eller med --flow. Bronze avgörs före Silver
  // (BRZ-4): Silver sänker en funktion vars krav inte håller före sin
  // fixpunkt, så att anropare som bygger på dess eftervillkor också faller.
  std::map<std::string, FlowGraph> graphs;
  bool requirements = false;
  for (const auto& [usr, f] : *facts) {
    requirements = requirements || f.declaresDepends;
    if (f.flow) graphs.emplace(usr, *f.flow);
  }
  std::map<std::string, FlowSummary> flows;
  if (!graphs.empty() && (showFlow || requirements)) flows = computeFlows(graphs);
  if (showFlow)
    for (const auto& [usr, s] : flows)
      if (verdict.at(usr).isStone())
        verdict.at(usr).flow = s.known ? flowLines(s) : std::vector<std::string>{"not computed: " + s.why};
  for (const auto& [usr, f] : *facts) {
    Verdict& v = verdict.at(usr);
    if (!f.declaresDepends) continue;
    // BRZ-6: ett krav i en funktion som inte är Stone (också ett felplacerat
    // krav) eller vars kropp är betrodd håller inte.
    if (v.kind == Verdict::Kind::trusted) {
      v.bronze = "the body is trusted, not analysed";
      continue;
    }
    // Ett krav utanför inledningen (konstruktorinitierare, sizeof, lambda,
    // efter en vanlig sats) läses inte av flödet: det håller inte.
    if (f.misplacedDepends) {
      v.bronze = "PORK_DEPENDS must come before all other statements";
      continue;
    }
    if (!v.isStone()) {
      v.bronze = "the function is not Stone";
      continue;
    }
    auto s = flows.find(usr);
    auto g = graphs.find(usr);
    if (s == flows.end() || g == graphs.end() || g->second.requirements.empty()) {
      v.bronze = "PORK_DEPENDS was not read";  // ska inte hända: konservativt
      continue;
    }
    v.bronze = "";
    for (const FlowRequirement& r : g->second.requirements)
      if (std::string why = checkRequirement(s->second, r); !why.empty()) {
        v.bronze = why;
        break;
      }
  }

  if (prover != nullptr) silver(*facts, verdict, *prover, jobs, inScope);

  // Oraklet prövar flödet: också en funktion vars krav inte håller har ett
  // flöde att pröva (den är Stone, och dess C finns).
  if (oracle != OracleMode::off && prover != nullptr && !flows.empty()) {
    std::map<std::string, FlowSummary> tested;
    for (const auto& [usr, s] : flows)
      if (verdict.at(usr).isStone() && inScope(facts->at(usr))) tested.emplace(usr, s);
    flowOracle(*facts, verdict, tested, *prover, jobs, oracle == OracleMode::selfTest);
    // Ett krav som håller är inte starkare än flödet: säg när oraklet
    // kördes men inte kunde pröva det.
    for (const auto& [usr, s] : tested) {
      Verdict& v = verdict.at(usr);
      if (v.bronze && v.bronze->empty() &&
          std::ranges::none_of(v.flowOracle, [](const std::string& o) { return o.starts_with("ok"); }))
        v.flowOracle.push_back("the flow requirements rest on a flow the oracle did not check");
    }
  }

  std::vector<const FunctionFacts*> order;
  order.reserve(facts->size());
  for (const auto& [usr, f] : *facts)
    if (inScope(f)) order.push_back(&f);
  std::ranges::sort(order, {}, [](const FunctionFacts* f) {
    return std::tie(f->file, f->line, f->column, f->name);
  });

  for (const FunctionFacts* f : order) report.function(*f, verdict.at(f->usr));

  int rc = ok;
  for (const FunctionFacts* f : order) {
    const Verdict& v = verdict.at(f->usr);
    bool notBronze = v.bronze && !v.bronze->empty();  // BRZ-6
    if ((f->required && !v.isStone()) || notBronze) {
      report.violation(*f, v);
      rc = requirementViolated;
    }
  }
  // Ett mallmönsters placeringsfel står redan på en instans som rapporterar
  // samma förekomst; bara instanser vars fel faktiskt skrevs ut räknas.
  std::set<std::string> reported;
  for (const FunctionFacts* f : order) {
    const Verdict& v = verdict.at(f->usr);
    if (v.bronze && !v.bronze->empty()) reported.insert(f->misplacedAt.begin(), f->misplacedAt.end());
  }
  // En förekomst en gång, bland dem som är kvar efter --only (i sortering-
  // sordning: mönstret "Tmpl::v" före instanserna "Tmpl<int>::v").
  std::set<std::string> printed;
  for (const StrayRequirement& s : source.strayRequirements())
    if ((only.empty() || underAny(s.file, only)) && !(s.inTemplate && reported.contains(s.occurrence)) &&
        printed.insert(s.occurrence).second) {
      report.strayRequirement(s);
      rc = requirementViolated;
    }
  return rc;
}

}  // namespace pork
