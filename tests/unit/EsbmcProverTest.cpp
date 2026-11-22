#include "EsbmcProver.hpp"

#include <gtest/gtest.h>

using pork::EsbmcProver;
using pork::ProofFailure;

namespace {

constexpr unsigned kUnwind = 64;
constexpr unsigned kMemory = 4096;

// Utdrag ur riktig esbmc 8.6-utdata (--multi-property --quiet).
constexpr const char* kFailed = R"(Starting Bounded Model Checking
** Results:
/src/corpus/20_silver.cpp, function f_vadd__entry
  FAILED       [f_vadd__entry.arithmetic-overflow.1]  line 69  arithmetic overflow on floating-point ieee_add
  PASSED       [f_vadd__entry.nan.1]                  line 69  NaN on ieee_add
  FAILED       [f_vadd__entry.arithmetic-overflow.2]  line 70  arithmetic overflow on add

** 2 of 3 properties failed, 1 passed

VERIFICATION FAILED
ESBMC version 8.6.0 64-bit x86_64 linux
)";

}  // namespace

TEST(EsbmcProver, SuccessWithoutTableIsProved) {
  auto r = EsbmcProver::parseOutput("Generated 0 VCC(s)\n\nVERIFICATION SUCCESSFUL\n", 0, kUnwind, kMemory);
  ASSERT_TRUE(r.has_value());
  EXPECT_TRUE(r->empty());
}

TEST(EsbmcProver, FailedPropertiesAreReportedInPorksWording) {
  auto r = EsbmcProver::parseOutput(kFailed, 1, kUnwind, kMemory);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->size(), 2u);
  EXPECT_EQ((*r)[0], (ProofFailure{"arithmetic overflow on floating-point addition", "/src/corpus/20_silver.cpp", 69, ""}));
  EXPECT_EQ((*r)[1], (ProofFailure{"arithmetic overflow on signed +", "/src/corpus/20_silver.cpp", 70, ""}));
}

// ESBMC antar ett godtyckligt värde för en funktion utan kropp och kan då
// säga SUCCESSFUL. Det får aldrig bli ett bevis.
TEST(EsbmcProver, TrailingConditionIsDropped) {
  auto r = EsbmcProver::parseOutput(
      "** Results:\nf.cpp, function g\n  FAILED  [g.o.1]  line 3  arithmetic overflow on floating-point ieee_add "
      "[isinf(a.x) || !(isinf(IEEE_ADD(a.x, b.x)))]\n\nVERIFICATION FAILED\n",
      1, kUnwind, kMemory);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->size(), 1u);
  EXPECT_EQ((*r)[0].description, "arithmetic overflow on floating-point addition");
}

TEST(EsbmcProver, PorkContractsComeBeforeEsbmcsOwnChecks) {
  auto r = EsbmcProver::parseOutput(
      "** Results:\nf.cpp, function g\n"
      "  FAILED  [g.bounds.1]     line 24  dereference failure: array bounds violated\n"
      "  FAILED  [g.assertion.1]  line 24  precondition of 'std::vector::operator[]'\n"
      "\nVERIFICATION FAILED\n",
      1, kUnwind, kMemory);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->size(), 2u);
  EXPECT_EQ((*r)[0].description, "precondition of 'std::vector::operator[]'");
}

TEST(EsbmcProver, MissingBodyIsAFailureEvenWhenSuccessful) {
  auto r = EsbmcProver::parseOutput(
      "WARNING: no body for function external\nWARNING: no body for function external\n\nVERIFICATION SUCCESSFUL\n", 0,
      kUnwind, kMemory);
  ASSERT_TRUE(r.has_value());
  ASSERT_EQ(r->size(), 1u);
  EXPECT_EQ((*r)[0].missingFunction, "external");
}

TEST(EsbmcProver, UndecidedPropertyWithoutFailureIsAnError) {
  auto r = EsbmcProver::parseOutput(
      "** Results:\nf.c, function g\n  UNKNOWN      [g.assertion.1]  line 3  x > 0\n\nVERIFICATION SUCCESSFUL\n", 0,
      kUnwind, kMemory);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().message.find("could not decide 'x > 0'"), std::string::npos);
}

TEST(EsbmcProver, FailedVerdictWithoutFailedPropertyIsAnError) {
  EXPECT_FALSE(EsbmcProver::parseOutput("VERIFICATION FAILED\n", 1, kUnwind, kMemory).has_value());
}

TEST(EsbmcProver, ParseErrorIsReported) {
  auto r = EsbmcProver::parseOutput("bad.c:1:31: error: use of undeclared identifier 'syntax'\n", 6, kUnwind, kMemory);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().message.find("undeclared identifier"), std::string::npos);
  EXPECT_FALSE(r.error().timedOut);
}

TEST(EsbmcProver, SilentCrashUnderMemoryLimitSuggestsMoreMemory) {
  auto r = EsbmcProver::parseOutput("Starting Bounded Model Checking\n", 134, kUnwind, kMemory);
  ASSERT_FALSE(r.has_value());
  EXPECT_NE(r.error().message.find("--silver-memory"), std::string::npos);
}

TEST(EsbmcProver, NormalizesEsbmcDescriptions) {
  EXPECT_EQ(EsbmcProver::normalize("arithmetic overflow on floating-point ieee_div", 8),
            "arithmetic overflow on floating-point division");
  EXPECT_EQ(EsbmcProver::normalize("arithmetic overflow on neg", 8), "arithmetic overflow on signed unary minus");
  // --silver-strict: ESBMC säger inte om operanden var signed eller unsigned.
  EXPECT_EQ(EsbmcProver::normalize("arithmetic overflow on add", 8, true), "arithmetic overflow on integer +");
  EXPECT_EQ(EsbmcProver::normalize("NaN on ieee_mul", 8), "NaN on multiplication");
  EXPECT_EQ(EsbmcProver::normalize("dereference failure: NULL pointer", 8), "dereference failure: pointer NULL");
  EXPECT_EQ(EsbmcProver::normalize("unwinding assertion loop 3", 8),
            "loop may need more than 8 iterations (raise --unwind)");
  EXPECT_EQ(EsbmcProver::normalize("division by zero", 8), "division by zero");
  EXPECT_EQ(EsbmcProver::normalize("arithmetic overflow on shl", 8), "shift distance, operand or result out of range");
  EXPECT_EQ(EsbmcProver::normalize("precondition of 'half'", 8), "precondition of 'half'");
}

TEST(EsbmcProver, KnownSolvers) {
  EXPECT_TRUE(EsbmcProver::knownSolver("bitwuzla"));
  EXPECT_TRUE(EsbmcProver::knownSolver("z3"));
  EXPECT_FALSE(EsbmcProver::knownSolver("minisat"));
  EXPECT_FALSE(EsbmcProver::knownSolver("cadical"));
}

TEST(EsbmcProver, SolverIsPartOfTheConfiguration) {
  EsbmcProver::Options a, b;
  b.solver = "z3";
  EXPECT_NE(EsbmcProver(a).configuration(), EsbmcProver(b).configuration());
}

TEST(EsbmcProver, TimeoutIsNotPartOfTheConfiguration) {
  EsbmcProver::Options a, b;
  b.timeoutSeconds = 600;
  EXPECT_EQ(EsbmcProver(a).configuration(), EsbmcProver(b).configuration());
  EXPECT_EQ(EsbmcProver(b).timeoutSeconds(), 600u);
}
