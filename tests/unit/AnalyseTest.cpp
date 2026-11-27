#include "Analyse.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

class FakeSource : public pork::IFactsSource {
 public:
  std::expected<pork::FactsMap, std::string> result;
  std::vector<pork::StrayRequirement> stray;
  std::expected<pork::FactsMap, std::string> collect() override { return result; }
  std::vector<pork::StrayRequirement> strayRequirements() const override { return stray; }
};

class FakeReport : public pork::IReport {
 public:
  std::vector<std::string> functions;
  std::vector<std::string> violations;
  std::vector<std::string> errors;
  std::vector<std::string> strays;
  void function(const pork::FunctionFacts& f, const pork::Verdict&) override {
    functions.push_back(f.name);
  }
  void violation(const pork::FunctionFacts& f, const pork::Verdict&) override {
    violations.push_back(f.name);
  }
  void error(const std::string& m) override { errors.push_back(m); }
  void strayRequirement(const pork::StrayRequirement& s) override { strays.push_back(s.where); }
  std::vector<std::string> warnings;
  void warning(const pork::Warning& w) override { warnings.push_back(w.message); }
};

pork::FunctionFacts at(const std::string& name, const std::string& file, unsigned line) {
  pork::FunctionFacts f;
  f.usr = "usr:" + name;
  f.name = name;
  f.file = file;
  f.line = line;
  f.column = 1;
  return f;
}

pork::FactsMap factsOf(std::initializer_list<pork::FunctionFacts> fs) {
  pork::FactsMap m;
  for (const auto& f : fs) m.emplace(f.usr, f);
  return m;
}

}  // namespace

TEST(Analyse, ReportsEveryFunctionSortedByFileAndLine) {
  FakeSource src;
  // usr-ordningen (a, b, c) skiljer sig medvetet från källordningen.
  src.result = factsOf({at("a", "y.cpp", 1), at("b", "x.cpp", 9), at("c", "x.cpp", 2)});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::ok);

  EXPECT_EQ(rep.functions, (std::vector<std::string>{"c", "b", "a"}));
  EXPECT_TRUE(rep.violations.empty());
  EXPECT_TRUE(rep.errors.empty());
}

TEST(Analyse, SourceFailureGivesErrorAndNoVerdicts) {
  FakeSource src;
  src.result = std::unexpected("compilation failed");
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::analysisFailed);

  EXPECT_TRUE(rep.functions.empty());
  EXPECT_EQ(rep.errors, (std::vector<std::string>{"compilation failed"}));
}

TEST(Analyse, RequiredFunctionThatIsStoneIsNoViolation) {
  auto f = at("a", "x.cpp", 1);
  f.required = true;
  FakeSource src;
  src.result = factsOf({f});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::ok);
  EXPECT_TRUE(rep.violations.empty());
}

TEST(Analyse, RequiredFunctionThatIsNotStoneIsViolation) {
  auto bad = at("bad", "x.cpp", 2);
  bad.required = true;
  bad.local = "uses global";
  auto other = at("other", "x.cpp", 1);
  other.local = "uses global";  // inte märkt: inget kravbrott
  FakeSource src;
  src.result = factsOf({bad, other});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::requirementViolated);
  EXPECT_EQ(rep.functions, (std::vector<std::string>{"other", "bad"}));  // alla rapporteras ändå
  EXPECT_EQ(rep.violations, (std::vector<std::string>{"bad"}));
}

// BRZ-6: ett PORK_DEPENDS-krav som inte håller är ett kravbrott (exit 1);
// ett som håller, eller inget krav, är det inte.
TEST(Analyse, FlowRequirementThatDoesNotHoldIsViolation) {
  auto graph = [](std::vector<std::string> allowed) {
    pork::FlowGraph g;
    g.blocks.resize(1);
    g.entry = g.exit = 0;
    g.params = {"price", "discount"};
    g.outputs = {false, false};
    g.returnsValue = true;
    g.blocks[0].stmts.push_back(
        {pork::FlowStmt::Kind::ret, {}, {pork::FlowValue::read("price"), pork::FlowValue::read("discount")}, {}});
    g.requirements.push_back({"return", std::move(allowed), {}});
    return g;
  };
  auto bad = at("bad", "x.cpp", 2);
  bad.flow = graph({"price"});
  bad.declaresDepends = true;
  auto good = at("good", "x.cpp", 1);
  good.flow = graph({"price", "discount"});
  good.declaresDepends = true;
  FakeSource src;
  src.result = factsOf({bad, good});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::requirementViolated);
  EXPECT_EQ(rep.violations, (std::vector<std::string>{"bad"}));
}

// BRZ-7: en sats utan verkan i en Stone-funktion ger en varning också utan
// krav, och varken domen eller exitstatus ändras. Två instanser av en mall
// med samma plats ger en varning.
TEST(Analyse, StatementWithoutEffectIsAWarning) {
  auto graph = [] {
    pork::FlowGraph g;
    g.blocks.resize(1);
    g.entry = g.exit = 0;
    g.params = {"price"};
    g.outputs = {false};
    g.returnsValue = true;
    g.blocks[0].stmts.push_back({pork::FlowStmt::Kind::assign, "t~1", {pork::FlowValue::read("price")}, {},
                                 {"the initialisation of 't'", 4, 3}});
    g.blocks[0].stmts.push_back({pork::FlowStmt::Kind::ret, {}, {pork::FlowValue::read("price")}, {}});
    return g;
  };
  auto a = at("f<int>", "x.cpp", 3);
  a.flow = graph();
  auto b = at("f<long>", "x.cpp", 3);
  b.flow = graph();
  FakeSource src;
  src.result = factsOf({a, b});

  FakeReport rep;
  EXPECT_EQ(pork::analyse(src, {}, rep), pork::ok);
  EXPECT_EQ(rep.warnings, (std::vector<std::string>{"the initialisation of 't' has no effect"}));
  EXPECT_TRUE(rep.violations.empty());

  FakeReport quiet;
  EXPECT_EQ(pork::analyse(src, {}, quiet, nullptr, 1, {}, pork::OracleMode::off, false, false), pork::ok);
  EXPECT_TRUE(quiet.warnings.empty());
}

// BRZ-6: ett krav i en funktion som inte blev Stone (global, felplacerat
// krav) eller vars kropp är betrodd håller inte.
TEST(Analyse, FlowRequirementInFunctionThatIsNotStoneIsViolation) {
  auto bad = at("bad", "x.cpp", 1);
  bad.local = "uses global";
  bad.declaresDepends = true;
  auto trusted = at("trusted", "x.cpp", 2);
  trusted.trusted = true;
  trusted.declaresDepends = true;
  FakeSource src;
  src.result = factsOf({bad, trusted});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::requirementViolated);
  EXPECT_EQ(rep.violations, (std::vector<std::string>{"bad", "trusted"}));
}

// BRZ-6: ett krav utanför varje funktionskropp är ett kravbrott; bara de
// inom --only rapporteras.
TEST(Analyse, StrayRequirementIsViolation) {
  FakeSource src;
  src.result = factsOf({at("f", "x.cpp", 1)});
  src.stray = {{"x.cpp", 3, 11, "the initializer of field 'Cfg::b'"}, {"y.cpp", 1, 1, "elsewhere"}};
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"x.cpp"}), pork::requirementViolated);
  EXPECT_EQ(rep.strays, (std::vector<std::string>{"the initializer of field 'Cfg::b'"}));
}

// Ett mallmönsters placeringsfel släpps bara när en instans fel för samma
// förekomst faktiskt skrivs ut: inte för en funktion utanför --only, inte
// för en annan förekomst med samma visningsläge.
TEST(Analyse, TemplatePlacementErrorDroppedOnlyForReportedInstance) {
  pork::StrayRequirement pattern{"in/t.cpp", 10, 41, "the body of 'tl'", "must come before all other statements",
                                 true, "usr:tl@in/t.cpp:120"};
  auto instance = [](const std::string& name, const std::string& file, std::string occurrence) {
    auto f = at(name, file, 10);
    f.declaresDepends = true;
    f.misplacedDepends = true;
    f.misplacedAt = {std::move(occurrence)};
    return f;
  };
  {  // instansen skrivs ut: mönstrets fel släpps
    FakeSource src;
    src.result = factsOf({instance("tl<int>", "in/t.cpp", "usr:tl@in/t.cpp:120")});
    src.stray = {pattern};
    FakeReport rep;
    EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"in"}), pork::requirementViolated);
    EXPECT_EQ(rep.violations, (std::vector<std::string>{"tl<int>"}));
    EXPECT_TRUE(rep.strays.empty());
  }
  {  // samma förekomst, men funktionen ligger utanför --only: felet står kvar
    FakeSource src;
    src.result = factsOf({instance("tl<int>", "out/t.cpp", "usr:tl@in/t.cpp:120")});
    src.stray = {pattern};
    FakeReport rep;
    EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"in"}), pork::requirementViolated);
    EXPECT_EQ(rep.strays, (std::vector<std::string>{"the body of 'tl'"}));
  }
  {  // en annan förekomst (samma visningsläge efter #line): felet står kvar
    FakeSource src;
    src.result = factsOf({instance("po", "in/t.cpp", "@out/t.cpp:120")});
    src.stray = {pattern};
    FakeReport rep;
    EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"in"}), pork::requirementViolated);
    EXPECT_EQ(rep.strays, (std::vector<std::string>{"the body of 'tl'"}));
  }
}

// Samma förekomst med två visningslägen (#line i två TU:er): den inom
// --only skrivs ut, en gång, oavsett ordning.
TEST(Analyse, StrayOccurrenceDeduplicatedAfterOnly) {
  for (bool insideFirst : {true, false}) {
    pork::StrayRequirement in{"in/h.hpp", 3, 5, "the initializer of field 'S::v'", "is outside a function body",
                              false, "@h.hpp:40"};
    pork::StrayRequirement out = in;
    out.file = "out/h.hpp";
    FakeSource src;
    src.result = factsOf({});
    src.stray = insideFirst ? std::vector{in, out} : std::vector{out, in};
    FakeReport rep;
    EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"in"}), pork::requirementViolated);
    EXPECT_EQ(rep.strays.size(), 1U);
  }
  FakeSource src;
  src.result = factsOf({});
  src.stray = {{"in/h.hpp", 3, 5, "a", "is outside a function body", false, "@h.hpp:40"},
               {"in/g.hpp", 3, 5, "b", "is outside a function body", false, "@h.hpp:40"}};
  FakeReport rep;
  pork::analyse(src, {}, rep);
  EXPECT_EQ(rep.strays.size(), 1U);  // två visningslägen inom --only: en gång
}

TEST(Analyse, RequiredFunctionThatIsOnlyTrustedIsViolation) {
  auto f = at("t", "x.cpp", 1);
  f.required = true;
  f.trusted = true;
  FakeSource src;
  src.result = factsOf({f});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep), pork::requirementViolated);
  EXPECT_EQ(rep.violations, (std::vector<std::string>{"t"}));
}

namespace {
class ProvingProver : public pork::IProver {
 public:
  pork::ProofResult prove(const std::string&) override { return std::vector<pork::ProofFailure>{}; }
  std::string configuration() const override { return "proving"; }
};
}  // namespace

TEST(Analyse, RequiredFunctionThatIsSilverIsNoViolation) {
  auto f = at("a", "x.cpp", 1);
  f.required = true;
  f.c = pork::CFunction{};
  FakeSource src;
  src.result = factsOf({f});
  FakeReport rep;
  ProvingProver prover;

  EXPECT_EQ(pork::analyse(src, {}, rep, &prover), pork::ok);
  EXPECT_TRUE(rep.violations.empty());
}

TEST(Analyse, OnlyReportsFunctionsUnderTheGivenDirectories) {
  FakeSource src;
  src.result = factsOf({at("own", "/repo/src/a.cpp", 1), at("header", "/repo/include/a.hpp", 1),
                        at("dep", "/repo/build/_deps/z.hpp", 1), at("sibling", "/repo/src2/b.cpp", 1)});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"/repo/src", "/repo/include/"}), pork::ok);

  EXPECT_EQ(rep.functions, (std::vector<std::string>{"header", "own"}));
}

TEST(Analyse, OnlyStillClassifiesThroughCalleesOutside) {
  FakeSource src;
  pork::FunctionFacts caller = at("caller", "/repo/src/a.cpp", 1);
  caller.callees.emplace("usr:dep", pork::Callee{"dep", "dep", false});
  pork::FunctionFacts dep = at("dep", "/deps/z.hpp", 1);
  dep.local = std::string("uses variable 'g'");
  src.result = factsOf({caller, dep});
  struct Verdicts : FakeReport {
    std::vector<pork::Verdict> verdicts;
    void function(const pork::FunctionFacts& f, const pork::Verdict& v) override {
      FakeReport::function(f, v);
      verdicts.push_back(v);
    }
  } rep;

  pork::analyse(src, {}, rep, nullptr, 1, {"/repo"});

  ASSERT_EQ(rep.functions, (std::vector<std::string>{"caller"}));
  EXPECT_FALSE(rep.verdicts[0].isStone());
}

TEST(Analyse, OnlyChecksRequirementsInScope) {
  FakeSource src;
  pork::FunctionFacts dep = at("dep", "/deps/z.hpp", 1);
  dep.required = true;
  dep.local = std::string("uses variable 'g'");
  src.result = factsOf({dep});
  FakeReport rep;

  EXPECT_EQ(pork::analyse(src, {}, rep, nullptr, 1, {"/repo"}), pork::ok);
  EXPECT_TRUE(rep.violations.empty());
}

TEST(UnderAny, ComparesWholePathComponentsAfterNormalising) {
  EXPECT_TRUE(pork::underAny("/repo/src/a.cpp", {"/repo/src"}));
  EXPECT_TRUE(pork::underAny("/repo/build/../src/a.cpp", {"/repo/src/"}));
  EXPECT_FALSE(pork::underAny("/repo/src2/a.cpp", {"/repo/src"}));
  EXPECT_FALSE(pork::underAny("/other/a.cpp", {"/repo"}));
  EXPECT_FALSE(pork::underAny("/repo/a.cpp", {}));
}
