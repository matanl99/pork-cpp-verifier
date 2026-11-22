#include "Silver.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using pork::CFunction;
using pork::FactsMap;
using pork::FunctionFacts;
using pork::ProofFailure;
using pork::Verdict;

namespace {

class FakeProver : public pork::IProver {
 public:
  pork::ProofResult answer = std::vector<ProofFailure>{};
  std::vector<std::string> programs;
  std::string configuration() const override { return "fake"; }
  pork::ProofResult prove(const std::string& p) override {
    programs.push_back(p);
    return answer;
  }
};

// En funktion med en översättning där varje del bär funktionens namn.
FunctionFacts fn(const std::string& name, std::initializer_list<std::string> callees = {}) {
  FunctionFacts f;
  f.usr = "usr:" + name;
  f.name = name;
  for (const auto& c : callees) f.callees.emplace("usr:" + c, pork::Callee{c, c, false});
  CFunction c;
  c.name = "c_" + name;
  c.decls = {{"T", "<type T>"}, {"type:" + name, "<type " + name + ">"}};
  c.prototype = "<proto " + name + ">";
  c.definition = "<def " + name + ">";
  c.entry = "<entry " + name + ">";
  f.c = c;
  return f;
}

struct Fixture {
  std::map<std::string, Verdict> verdicts;
  FakeProver prover;
};

void run(Fixture& r, std::initializer_list<FunctionFacts> fs) {
  FactsMap facts;
  for (const auto& f : fs) {
    facts.emplace(f.usr, f);
    r.verdicts.emplace(f.usr, Verdict::stone());
  }
  pork::silver(facts, r.verdicts, r.prover);
}

}  // namespace

TEST(Silver, ProvedStoneFunctionBecomesSilver) {
  Fixture r;
  run(r, {fn("a")});
  EXPECT_EQ(r.verdicts.at("usr:a").kind, Verdict::Kind::silver);
  ASSERT_EQ(r.prover.programs.size(), 1u);
  EXPECT_EQ(r.prover.programs[0], "<type T><type a><proto a><def a><entry a>");
}

TEST(Silver, ProgramContainsCalleesTypesOnceAndOnlyRootEntry) {
  Fixture r;
  run(r, {fn("a", {"b"}), fn("b")});
  EXPECT_EQ(r.prover.programs[0],
            "<type T><type a><type b><proto a><proto b><def a><def b><entry a>");
}

TEST(Silver, FailureKeepsStoneWithReasonAndLocation) {
  Fixture r;
  r.prover.answer = std::vector<ProofFailure>{{"overflow in a + b", "/src/x.cpp", 7, ""},
                                              {"NaN", "/src/x.cpp", 8, ""}};
  run(r, {fn("a")});
  const Verdict& v = r.verdicts.at("usr:a");
  EXPECT_EQ(v.kind, Verdict::Kind::stone);
  EXPECT_EQ(v.reason, "overflow in a + b at x.cpp:7 (+1 more)");
}

TEST(Silver, MissingBodyIsReportedAsMissingContractWithCppName) {
  Fixture r;
  FunctionFacts a = fn("a");
  a.c->value().contracts = {{"std__hypot__float__float", "std::hypot"}};
  r.prover.answer = std::vector<ProofFailure>{{"no body", "", 0, "std__hypot__float__float"}};
  run(r, {a});
  EXPECT_EQ(r.verdicts.at("usr:a").reason, "no contract for 'std::hypot'");
}

TEST(Silver, UntranslatableCalleeIsNamed) {
  Fixture r;
  FunctionFacts b = fn("b");
  b.c = std::unexpected(std::string("statement X is not supported"));
  run(r, {fn("a", {"b"}), b});
  EXPECT_EQ(r.verdicts.at("usr:a").reason, "cannot translate 'b' to C: statement X is not supported");
  EXPECT_EQ(r.verdicts.at("usr:b").reason, "cannot translate to C: statement X is not supported");
  EXPECT_TRUE(r.prover.programs.empty());
}

TEST(Silver, TrustedCalleeIsLeftToItsContract) {
  Fixture r;
  FunctionFacts t = fn("t");
  t.trusted = true;
  t.c.reset();
  FactsMap facts{{"usr:a", fn("a", {"t"})}, {"usr:t", t}};
  r.verdicts = {{"usr:a", Verdict::stone()}, {"usr:t", Verdict::trust()}};
  pork::silver(facts, r.verdicts, r.prover);
  EXPECT_EQ(r.verdicts.at("usr:a").kind, Verdict::Kind::silver);
  EXPECT_EQ(r.verdicts.at("usr:t").kind, Verdict::Kind::trusted);
  EXPECT_EQ(r.prover.programs.size(), 1u);
}

TEST(Silver, NotStoneIsNotProved) {
  Fixture r;
  FactsMap facts{{"usr:a", fn("a")}};
  r.verdicts = {{"usr:a", Verdict::notStone("uses global")}};
  pork::silver(facts, r.verdicts, r.prover);
  EXPECT_EQ(r.verdicts.at("usr:a"), Verdict::notStone("uses global"));
  EXPECT_TRUE(r.prover.programs.empty());
}

TEST(Silver, ProverErrorKeepsStone) {
  Fixture r;
  r.prover.answer = std::unexpected(pork::ProverError{"cbmc not found"});
  run(r, {fn("a")});
  EXPECT_EQ(r.verdicts.at("usr:a").kind, Verdict::Kind::stone);
  EXPECT_EQ(r.verdicts.at("usr:a").reason, "prover failed: cbmc not found");
}

TEST(Silver, ModelAssumptionsOfAllPartsAreAddedWhenProved) {
  Fixture r;
  FunctionFacts a = fn("a", {"b"});
  FunctionFacts b = fn("b");
  b.c->value().assumptions = {"std::vector size <= 16"};
  run(r, {a, b});
  EXPECT_EQ(r.verdicts.at("usr:a").assumptions, (std::set<std::string>{"std::vector size <= 16"}));
}

TEST(Silver, ModelAssumptionsAreNotAddedWhenNotProved) {
  Fixture r;
  FunctionFacts a = fn("a");
  a.c->value().assumptions = {"std::vector size <= 16"};
  r.prover.answer = std::vector<ProofFailure>{{"overflow", "x.cpp", 1, ""}};
  run(r, {a});
  EXPECT_TRUE(r.verdicts.at("usr:a").assumptions.empty());
}

namespace {

// Svarar efter programmets innehåll, och räknar hur många som körs samtidigt.
class ConcurrentProver : public pork::IProver {
 public:
  std::atomic<int> running{0};
  std::atomic<int> peak{0};
  std::string configuration() const override { return "concurrent"; }
  pork::ProofResult prove(const std::string& p) override {
    int now = ++running;
    for (int seen = peak; now > seen && !peak.compare_exchange_weak(seen, now);) {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    --running;
    if (p.find("<def bad") != std::string::npos)
      return std::vector<ProofFailure>{{"overflow", "a.cpp", 3, ""}};
    return std::vector<ProofFailure>{};
  }
};

}  // namespace

TEST(Silver, ParallelJobsGiveSameVerdictsAsSequential) {
  FactsMap facts;
  std::map<std::string, Verdict> verdicts;
  for (const auto* name : {"a", "bad1", "b", "bad2", "c", "d"}) {
    FunctionFacts f = fn(name);
    facts.emplace(f.usr, f);
    verdicts.emplace(f.usr, Verdict::stone());
  }
  ConcurrentProver prover;
  pork::silver(facts, verdicts, prover, 4);
  EXPECT_GT(prover.peak.load(), 1);
  EXPECT_LE(prover.peak.load(), 4);
  for (const auto* ok : {"a", "b", "c", "d"}) EXPECT_EQ(verdicts.at(std::string("usr:") + ok).kind, Verdict::Kind::silver);
  for (const auto* bad : {"bad1", "bad2"}) {
    const Verdict& v = verdicts.at(std::string("usr:") + bad);
    EXPECT_EQ(v.kind, Verdict::Kind::stone);
    EXPECT_EQ(v.reason, "overflow at a.cpp:3");
  }
}

namespace {

// Underkänner program som innehåller definitionen av en funktion vars namn
// börjar med "bad" (dess eget eftervillkor håller inte).
class ByContentProver : public pork::IProver {
 public:
  std::vector<std::string> programs;
  std::string configuration() const override { return "by-content"; }
  pork::ProofResult prove(const std::string& p) override {
    programs.push_back(p);
    if (p.find("<def bad") != std::string::npos)
      return std::vector<ProofFailure>{{"postcondition of 'bad'", "x.cpp", 1, ""}};
    return std::vector<ProofFailure>{};
  }
};

FunctionFacts withPost(FunctionFacts f) {
  (*f.c)->contract = "<contract " + f.name + ">";
  return f;
}

std::map<std::string, Verdict> runAll(ByContentProver& prover, std::initializer_list<FunctionFacts> fs) {
  FactsMap facts;
  std::map<std::string, Verdict> verdicts;
  for (const auto& f : fs) {
    facts.emplace(f.usr, f);
    verdicts.emplace(f.usr, Verdict::stone());
  }
  pork::silver(facts, verdicts, prover);
  return verdicts;
}

}  // namespace

TEST(Silver, CalleeWithPostIsUsedAsContractAndNotFollowed) {
  ByContentProver prover;
  runAll(prover, {fn("a", {"b"}), withPost(fn("b", {"c"})), fn("c")});
  auto it = std::ranges::find_if(prover.programs, [](const std::string& p) { return p.find("<entry a>") != std::string::npos; });
  ASSERT_NE(it, prover.programs.end());
  EXPECT_NE(it->find("<contract b>"), std::string::npos);
  EXPECT_EQ(it->find("<def b>"), std::string::npos);
  EXPECT_EQ(it->find("<def c>"), std::string::npos);  // b:s anrop följs inte
}

TEST(Silver, CalleeWithPostIsProvedWithItsOwnDefinition) {
  ByContentProver prover;
  runAll(prover, {withPost(fn("b"))});
  ASSERT_EQ(prover.programs.size(), 1u);
  EXPECT_NE(prover.programs[0].find("<def b>"), std::string::npos);
  EXPECT_EQ(prover.programs[0].find("<contract b>"), std::string::npos);
}

TEST(Silver, CallerOfUnprovedPostconditionIsNotSilverAlongTheChain) {
  ByContentProver prover;
  auto v = runAll(prover, {fn("a", {"b"}), withPost(fn("b", {"bad"})), withPost(fn("bad"))});
  EXPECT_EQ(v.at("usr:bad").kind, Verdict::Kind::stone);
  EXPECT_EQ(v.at("usr:b").kind, Verdict::Kind::stone);
  EXPECT_EQ(v.at("usr:b").reason, "relies on the unproven postcondition of 'bad'");
  EXPECT_EQ(v.at("usr:a").kind, Verdict::Kind::stone);
  EXPECT_EQ(v.at("usr:a").reason, "relies on the unproven postcondition of 'b'");
}

TEST(Silver, FunctionsTheConditionsCallAreIncludedEvenWithoutABodyCall) {
  ByContentProver prover;
  FunctionFacts a = fn("a");
  (*a.c)->contractCallees.insert("usr:inv");  // t.ex. en typinvariant
  runAll(prover, {a, fn("inv")});
  auto it = std::ranges::find_if(prover.programs, [](const std::string& p) { return p.find("<entry a>") != std::string::npos; });
  ASSERT_NE(it, prover.programs.end());
  EXPECT_NE(it->find("<def inv>"), std::string::npos);
}

namespace {

// Svarar på självtestet (<sat>) och beviset var för sig.
class SelfTestProver : public pork::IProver {
 public:
  SelfTestProver(std::vector<ProofFailure> s, std::vector<ProofFailure> p) : sat(std::move(s)), proof(std::move(p)) {}
  std::vector<ProofFailure> sat, proof;
  std::string configuration() const override { return "self-test"; }
  pork::ProofResult prove(const std::string& p) override {
    return p.find("<sat>") != std::string::npos ? sat : proof;
  }
};

Verdict withSelfTest(std::vector<ProofFailure> sat, std::vector<ProofFailure> proof = {}) {
  FunctionFacts a = fn("a");
  (*a.c)->satisfiable = "<sat>";
  FactsMap facts{{a.usr, a}};
  std::map<std::string, Verdict> verdicts{{a.usr, Verdict::stone()}};
  SelfTestProver prover(std::move(sat), std::move(proof));
  pork::silver(facts, verdicts, prover);
  return verdicts.at(a.usr);
}

}  // namespace

TEST(Silver, SelfTestThatFailsMeansThePreconditionsCanHold) {
  Verdict v = withSelfTest({{pork::kSatisfiableMarker, "x.cpp", 1, ""}});
  EXPECT_EQ(v.kind, Verdict::Kind::silver);
}

TEST(Silver, SelfTestThatHoldsMeansThePreconditionsContradict) {
  Verdict v = withSelfTest({});
  EXPECT_EQ(v.kind, Verdict::Kind::stone);
  EXPECT_NE(v.reason.find("preconditions can never hold"), std::string::npos);
}

TEST(Silver, ProofFailuresAreReportedWhenThePreconditionsCanHold) {
  Verdict v = withSelfTest({{pork::kSatisfiableMarker, "x.cpp", 1, ""}}, {{"division by zero", "x.cpp", 2, ""}});
  EXPECT_EQ(v.kind, Verdict::Kind::stone);
  EXPECT_EQ(v.reason, "division by zero at x.cpp:2");
}

TEST(Silver, WantedLimitsTheRootsButNotTheCallees) {
  Fixture r;
  FactsMap facts;
  for (const auto& f : {fn("root", {"helper"}), fn("helper")}) {
    facts.emplace(f.usr, f);
    r.verdicts.emplace(f.usr, Verdict::stone());
  }

  pork::silver(facts, r.verdicts, r.prover, 1, [](const FunctionFacts& f) { return f.name == "root"; });

  ASSERT_EQ(r.prover.programs.size(), 1u);
  EXPECT_NE(r.prover.programs[0].find("<def helper>"), std::string::npos);
  EXPECT_EQ(r.verdicts.at("usr:root").kind, Verdict::Kind::silver);
  EXPECT_EQ(r.verdicts.at("usr:helper"), Verdict::stone());
}

// Granskning 2: --only får inte sänka en vald funktion. Beviset för
// root bygger på dep:s eftervillkor; dep är inte vald men ska ändå bevisas,
// annars blir root "relies on the unproven postcondition of 'dep'".
TEST(Silver, WantedStillProvesThePostconditionsTheRootReliesOn) {
  ByContentProver prover;
  FactsMap facts;
  std::map<std::string, Verdict> verdicts;
  for (const auto& f : {fn("root", {"dep"}), withPost(fn("dep"))}) {
    facts.emplace(f.usr, f);
    verdicts.emplace(f.usr, Verdict::stone());
  }
  pork::silver(facts, verdicts, prover, 1, [](const FunctionFacts& f) { return f.name == "root"; });
  EXPECT_EQ(verdicts.at("usr:root").kind, Verdict::Kind::silver) << verdicts.at("usr:root").reason;
  EXPECT_EQ(verdicts.at("usr:dep").kind, Verdict::Kind::silver);
  EXPECT_EQ(prover.programs.size(), 2u);
}

// Och ett eftervillkor som inte håller sänker fortfarande den valda.
TEST(Silver, WantedRootIsStillLoweredByAFailingPostcondition) {
  ByContentProver prover;
  FactsMap facts;
  std::map<std::string, Verdict> verdicts;
  for (const auto& f : {fn("root", {"bad"}), withPost(fn("bad"))}) {
    facts.emplace(f.usr, f);
    verdicts.emplace(f.usr, Verdict::stone());
  }
  pork::silver(facts, verdicts, prover, 1, [](const FunctionFacts& f) { return f.name == "root"; });
  EXPECT_EQ(verdicts.at("usr:root").kind, Verdict::Kind::stone);
  EXPECT_EQ(verdicts.at("usr:root").reason, "relies on the unproven postcondition of 'bad'");
}
