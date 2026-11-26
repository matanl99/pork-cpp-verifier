#include "Classify.hpp"

#include <gtest/gtest.h>

using pork::Callee;
using pork::FactsMap;
using pork::FunctionFacts;
using pork::Reason;
using pork::TrustList;
using pork::Verdict;

namespace {

// fn("a", {"b", "c"}) = lokalt godkänd a som anropar b och c.
FunctionFacts fn(const std::string& name, std::initializer_list<std::string> callees = {},
                 Reason local = std::nullopt) {
  FunctionFacts f;
  f.usr = "usr:" + name;
  f.name = name;
  f.local = std::move(local);
  for (const auto& c : callees) f.callees.emplace("usr:" + c, Callee{c, c, false});
  return f;
}

FunctionFacts trustedFn(const std::string& name) {
  FunctionFacts f = fn(name, {}, "would fail if analysed");
  f.trusted = true;
  return f;
}

// a anropar en deklaration (utan definition) som är märkt pork::trusted.
FunctionFacts callsAnnotatedDecl(const std::string& name, const std::string& decl) {
  FunctionFacts f = fn(name);
  f.callees.emplace("usr:" + decl, Callee{decl, decl, true});
  return f;
}

std::map<std::string, Verdict> run(std::initializer_list<FunctionFacts> fs, TrustList trust = {}) {
  FactsMap facts;
  for (const auto& f : fs) facts.emplace(f.usr, f);
  return classify(facts, trust);
}

Verdict stoneAssuming(std::set<std::string> assumptions) {
  Verdict v = Verdict::stone();
  v.assumptions = std::move(assumptions);
  return v;
}

}  // namespace

TEST(Classify, LocallyOkLeafIsStone) {
  EXPECT_EQ(run({fn("a")}).at("usr:a"), Verdict::stone());
}

TEST(Classify, LocalReasonIsKept) {
  EXPECT_EQ(run({fn("a", {}, "uses global")}).at("usr:a"), Verdict::notStone("uses global"));
}

TEST(Classify, CallingUndefinedFunctionIsNotStone) {
  EXPECT_EQ(run({fn("a", {"missing"})}).at("usr:a"),
            Verdict::notStone("calls 'missing' with no definition in the analysed sources "
                              "(trust it with 'missing' in a trust file)"));
}

TEST(Classify, NonStoneSpreadsTransitivelyToCallers) {
  auto v = run({fn("a", {"b"}), fn("b", {"c"}), fn("c", {}, "uses global")});
  EXPECT_EQ(v.at("usr:b"), Verdict::notStone("calls non-stone 'c'"));
  EXPECT_EQ(v.at("usr:a"), Verdict::notStone("calls non-stone 'b'"));
}

TEST(Classify, StoneCalleeKeepsCallerStone) {
  EXPECT_EQ(run({fn("a", {"b"}), fn("b")}).at("usr:a"), Verdict::stone());
}

TEST(Classify, MutualRecursionBetweenOkFunctionsIsStone) {
  auto v = run({fn("even", {"odd"}), fn("odd", {"even"})});
  EXPECT_EQ(v.at("usr:even"), Verdict::stone());
  EXPECT_EQ(v.at("usr:odd"), Verdict::stone());
}

TEST(Classify, CycleWithOneBadMemberIsNotStone) {
  auto v = run({fn("a", {"b"}), fn("b", {"c"}), fn("c", {"a"}, "uses global")});
  EXPECT_EQ(v.at("usr:a").kind, Verdict::Kind::notStone);
  EXPECT_EQ(v.at("usr:b").kind, Verdict::Kind::notStone);
  EXPECT_EQ(v.at("usr:c"), Verdict::notStone("uses global"));
}

// --- Betrodda funktioner ---

TEST(Classify, UndefinedCalleeInTrustListBecomesAssumption) {
  auto v = run({fn("a", {"hypot"})}, TrustList{{"hypot"}, {}});
  EXPECT_EQ(v.at("usr:a"), stoneAssuming({"hypot"}));
}

TEST(Classify, UndefinedCalleeAnnotatedTrustedBecomesAssumption) {
  auto v = run({callsAnnotatedDecl("a", "ext")});
  EXPECT_EQ(v.at("usr:a"), stoneAssuming({"ext"}));
}

TEST(Classify, TrustedFunctionIgnoresItsOwnBodyAndCallees) {
  FunctionFacts t = trustedFn("t");
  t.callees.emplace("usr:missing", Callee{"missing", "missing", false});
  auto v = run({t, fn("a", {"t"})});
  EXPECT_EQ(v.at("usr:t"), Verdict::trust());
  EXPECT_EQ(v.at("usr:a"), stoneAssuming({"t"}));
}

TEST(Classify, TrustListDoesNotOverrideAnalysis) {
  auto v = run({fn("a", {"b"}), fn("b", {}, "uses global")}, TrustList{{"b"}, {}});
  EXPECT_EQ(v.at("usr:b"), Verdict::notStone("uses global"));
  EXPECT_EQ(v.at("usr:a"), Verdict::notStone("calls non-stone 'b'"));
}

TEST(Classify, AssumptionsPropagateTransitivelyAndMerge) {
  auto v = run({fn("top", {"left", "right"}), fn("left", {"hypot"}), fn("right", {"fabs"})},
               TrustList{{"hypot", "fabs"}, {}});
  EXPECT_EQ(v.at("usr:top"), stoneAssuming({"fabs", "hypot"}));
}

TEST(Classify, AssumptionsReachAllMembersOfACycle) {
  auto v = run({fn("even", {"odd"}), fn("odd", {"even", "hypot"})}, TrustList{{"hypot"}, {}});
  EXPECT_EQ(v.at("usr:even"), stoneAssuming({"hypot"}));
  EXPECT_EQ(v.at("usr:odd"), stoneAssuming({"hypot"}));
}

TEST(Classify, TrustListMatchesAndReportsQualifiedName) {
  FunctionFacts a = fn("a");
  a.callees.emplace("usr:clamp<float>", Callee{"std::clamp<float>", "std::clamp", false});
  a.callees.emplace("usr:clamp<int>", Callee{"std::clamp<int>", "std::clamp", false});
  auto v = run({a}, TrustList{{"std::clamp"}, {}});
  EXPECT_EQ(v.at("usr:a"), stoneAssuming({"std::clamp"}));  // en post, inte en per instans
}

TEST(Classify, TrustListWildcardCoversAllMembers) {
  FunctionFacts a = fn("a");
  a.callees.emplace("usr:reset", Callee{"std::optional<int>::reset", "std::optional::reset", false});
  a.callees.emplace("usr:ctor", Callee{"std::optional<int>::optional", "std::optional::optional", false});
  auto v = run({a}, TrustList{{"std::optional::*"}, {}});
  EXPECT_EQ(v.at("usr:a"), stoneAssuming({"std::optional::optional", "std::optional::reset"}));
}

TEST(Classify, WildcardDoesNotMatchOtherScopes) {
  FunctionFacts a = fn("a");
  a.callees.emplace("usr:x", Callee{"std::optionalish::f", "std::optionalish::f", false});
  auto v = run({a}, TrustList{{"std::optional::*"}, {}});
  EXPECT_EQ(v.at("usr:a").kind, Verdict::Kind::notStone);
}

TEST(Classify, AssumedTypesBecomeAssumptionsAndPropagate) {
  FunctionFacts leaf = fn("leaf");
  leaf.assumedTypes = {"type std::optional"};
  auto v = run({fn("top", {"leaf"}), leaf});
  EXPECT_EQ(v.at("usr:leaf"), stoneAssuming({"type std::optional"}));
  EXPECT_EQ(v.at("usr:top"), stoneAssuming({"type std::optional"}));
}

TEST(Classify, AssumedTypesDoNotRescueNonStone) {
  FunctionFacts f = fn("f", {}, "uses global");
  f.assumedTypes = {"type std::optional"};
  EXPECT_EQ(run({f}).at("usr:f"), Verdict::notStone("uses global"));
}
