#include "UnionCheckProver.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

class FakeProver : public pork::IProver {
 public:
  FakeProver(std::string name, pork::ProofResult answer) : name_(std::move(name)), answer_(std::move(answer)) {}
  std::vector<std::string> seen;
  pork::ProofResult prove(const std::string& p) override {
    seen.push_back(p);
    return answer_;
  }
  std::string configuration() const override { return name_; }
  unsigned timeoutSeconds() const override { return name_ == "main" ? 7 : 99; }

 private:
  std::string name_;
  pork::ProofResult answer_;
};

const pork::ProofResult proved = std::vector<pork::ProofFailure>{};
const pork::ProofResult failed = std::vector<pork::ProofFailure>{{"pork flow oracle", "a.cpp", 3, ""}};
const pork::ProofResult broke = std::unexpected(pork::ProverError{"timed out", true});
const std::string withUnion = "struct V { union { int a; } u; };";

}  // namespace

TEST(UnionCheckProver, HasUnionAsAWordOnly) {
  EXPECT_TRUE(pork::UnionCheckProver::hasUnion("struct V { union { int a; } u; };"));
  EXPECT_TRUE(pork::UnionCheckProver::hasUnion("union U x;"));
  EXPECT_FALSE(pork::UnionCheckProver::hasUnion("int reunion_count; int unions; int my_union;"));
  EXPECT_FALSE(pork::UnionCheckProver::hasUnion("int x;"));
}

// Bara nyckelordet i koden: inte i #line-sökvägar, strängar, tecken eller
// kommentarer.
TEST(UnionCheckProver, HasUnionIgnoresPathsStringsAndComments) {
  EXPECT_FALSE(pork::UnionCheckProver::hasUnion("#line 1 \"generated/union/input.cpp\"\nint f(int a) { return a; }"));
  EXPECT_FALSE(pork::UnionCheckProver::hasUnion("  #define X \\\n union\nint x;"));
  EXPECT_FALSE(pork::UnionCheckProver::hasUnion("__CPROVER_assert(0, \"a union \\\" union\");"));
  EXPECT_FALSE(pork::UnionCheckProver::hasUnion("char c = 'u'; // union\n/* union */ int y;"));
  EXPECT_TRUE(pork::UnionCheckProver::hasUnion("#line 1 \"a.cpp\"\nstruct V { union { int a; } u; };"));
  EXPECT_TRUE(pork::UnionCheckProver::hasUnion("/* x */ union U u; // y"));
}

TEST(UnionCheckProver, ProvedOrWithoutUnionIsNotChecked) {
  FakeProver main("main", proved), check("check", proved);
  pork::UnionCheckProver p(main, check);
  EXPECT_EQ(p.prove(withUnion), proved);
  FakeProver main2("main", failed);
  pork::UnionCheckProver p2(main2, check);
  EXPECT_EQ(p2.prove("int x;"), failed);
  EXPECT_TRUE(check.seen.empty());
}

TEST(UnionCheckProver, FailureWithUnionIsCheckedAndOverturnedOnlyWhenTheCheckProves) {
  FakeProver main("main", failed), yes("check", proved), no("check", failed), errs("check", broke);
  pork::UnionCheckProver a(main, yes), b(main, no), c(main, errs);
  EXPECT_EQ(a.prove(withUnion), proved);
  EXPECT_EQ(b.prove(withUnion), failed);
  // Kontrollen avbröts: inget slutresultat. Ett bevisarfel med cbmc:s fynd i
  // texten, och en tidsgräns förblir en tidsgräns (cachen prövar igen).
  pork::ProofResult r = c.prove(withUnion);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().timedOut);
  EXPECT_NE(r.error().message.find("pork flow oracle at a.cpp:3"), std::string::npos);
  EXPECT_NE(r.error().message.find("timed out"), std::string::npos);
  EXPECT_EQ(a.checked(), 1U);
  EXPECT_EQ(a.overturned(), 1U);
  EXPECT_EQ(b.overturned(), 0U);
}

TEST(UnionCheckProver, ProverErrorIsNotChecked) {
  FakeProver main("main", broke), check("check", proved);
  pork::UnionCheckProver p(main, check);
  EXPECT_FALSE(p.prove(withUnion).has_value());
  EXPECT_TRUE(check.seen.empty());
}

TEST(UnionCheckProver, ConfigurationNamesBothAndTimeoutIsTheMainOne) {
  FakeProver main("main", proved), check("check", proved);
  pork::UnionCheckProver p(main, check);
  EXPECT_EQ(p.configuration(), "main\nunion check:\ncheck");
  EXPECT_EQ(p.timeoutSeconds(), 7U);
}
