#include "CbmcProver.hpp"

#include <gtest/gtest.h>

using pork::CbmcProver;

TEST(CbmcProver, MinisatIsBuiltInAndNeedsNoFlags) {
  EXPECT_TRUE(CbmcProver::solverFlags("minisat").empty());
  EXPECT_TRUE(CbmcProver::solverFlags("").empty());
}

TEST(CbmcProver, KnownSmtSolversUseTheirOwnFlag) {
  EXPECT_EQ(CbmcProver::solverFlags("z3"), std::vector<std::string>{"--z3"});
  EXPECT_EQ(CbmcProver::solverFlags("cvc5"), std::vector<std::string>{"--cvc5"});
  EXPECT_EQ(CbmcProver::solverFlags("bitwuzla"), std::vector<std::string>{"--bitwuzla"});
}

TEST(CbmcProver, AnythingElseIsAnExternalSatSolver) {
  EXPECT_EQ(CbmcProver::solverFlags("cadical"), (std::vector<std::string>{"--external-sat-solver", "cadical"}));
  EXPECT_EQ(CbmcProver::solverFlags("/opt/kissat"), (std::vector<std::string>{"--external-sat-solver", "/opt/kissat"}));
}

TEST(CbmcProver, SolverIsPartOfTheConfiguration) {
  CbmcProver::Options a, b;
  b.solver = "cadical";
  EXPECT_NE(CbmcProver(a).configuration(), CbmcProver(b).configuration());
}

TEST(CbmcProver, TimeoutIsNotPartOfTheConfiguration) {
  CbmcProver::Options a, b;
  b.timeoutSeconds = 600;
  EXPECT_EQ(CbmcProver(a).configuration(), CbmcProver(b).configuration());
  EXPECT_EQ(CbmcProver(b).timeoutSeconds(), 600u);
}
