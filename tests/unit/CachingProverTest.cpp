#include "CachingProver.hpp"

#include <gtest/gtest.h>
#include <llvm/Support/FileSystem.h>

using pork::CachingProver;
using pork::ProofFailure;
using pork::ProofResult;
using pork::ProverError;

namespace {

class CountingProver : public pork::IProver {
 public:
  ProofResult answer = std::vector<ProofFailure>{};
  std::string config = "v1";
  unsigned timeout = 30;
  int calls = 0;
  ProofResult prove(const std::string&) override {
    ++calls;
    return answer;
  }
  std::string configuration() const override { return config; }
  unsigned timeoutSeconds() const override { return timeout; }
};

// En tom katalog per test, borttagen efteråt.
class CachingProverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    llvm::SmallString<128> d;
    ASSERT_FALSE(llvm::sys::fs::createUniqueDirectory("pork-cache-test", d));
    dir = std::string(d);
  }
  void TearDown() override { (void)llvm::sys::fs::remove_directories(dir); }

  CachingProver make(bool retryTimeouts = false) { return CachingProver(inner, {dir, retryTimeouts}); }

  std::string dir;
  CountingProver inner;
};

}  // namespace

TEST_F(CachingProverTest, SecondProveOfSameProgramComesFromCache) {
  inner.answer = std::vector<ProofFailure>{{"overflow in a + b", "/src/x.cpp", 7, "", "    a = 2147483647\n    b = 1\n"},
                                           {"no body", "", 0, "std__hypot__float__float"}};
  auto first = make().prove("int f(void);");
  auto again = make();
  auto second = again.prove("int f(void);");
  EXPECT_EQ(inner.calls, 1);
  EXPECT_EQ(again.hits(), 1u);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*second, *first);
}

TEST_F(CachingProverTest, ChangedProgramIsProvedAgain) {
  (void)make().prove("int f(void);");
  (void)make().prove("int f(int);");
  EXPECT_EQ(inner.calls, 2);
}

TEST_F(CachingProverTest, ChangedConfigurationIsProvedAgain) {
  (void)make().prove("int f(void);");
  inner.config = "v2";
  (void)make().prove("int f(void);");
  EXPECT_EQ(inner.calls, 2);
}

TEST_F(CachingProverTest, TimeoutIsSavedAndReportedAsCached) {
  inner.answer = std::unexpected(ProverError{"cbmc timed out after 30 s", true});
  (void)make().prove("slow");
  auto r = make().prove("slow");
  EXPECT_EQ(inner.calls, 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().timedOut);
  EXPECT_EQ(r.error().message, "cbmc timed out after 30 s (cached)");
}

TEST_F(CachingProverTest, RetryTimeoutsProvesSavedTimeoutAgainAndSavesNewAnswer) {
  inner.answer = std::unexpected(ProverError{"cbmc timed out after 30 s", true});
  (void)make().prove("slow");
  inner.answer = std::vector<ProofFailure>{};
  auto r = make(/*retryTimeouts=*/true).prove("slow");
  EXPECT_EQ(inner.calls, 2);
  EXPECT_TRUE(r.has_value());
  (void)make().prove("slow");  // det nya svaret (bevisat) är sparat
  EXPECT_EQ(inner.calls, 2);
}

TEST_F(CachingProverTest, OtherErrorsAreNotSaved) {
  inner.answer = std::unexpected(ProverError{"cbmc not found"});
  (void)make().prove("p");
  (void)make().prove("p");
  EXPECT_EQ(inner.calls, 2);
}

TEST_F(CachingProverTest, KeyDependsOnBothConfigurationAndProgram) {
  auto a = make().key("p");
  inner.config = "v2";
  auto b = make().key("p");
  EXPECT_NE(a, b);
  EXPECT_EQ(a.size(), 64u);
  // Gränsen mellan konfiguration och program är otvetydig.
  inner.config = "ab";
  auto c = make().key("c");
  inner.config = "a";
  EXPECT_NE(c, make().key("bc"));
}

// Snabbt pass + långsamt pass: tidsgränsen ingår inte i nyckeln.

TEST_F(CachingProverTest, KeyDoesNotDependOnTimeout) {
  auto a = make().key("p");
  inner.timeout = 600;
  EXPECT_EQ(a, make().key("p"));
}

TEST_F(CachingProverTest, AnswerIsReusedWithAnotherTimeout) {
  inner.timeout = 10;
  (void)make().prove("p");
  inner.timeout = 600;
  auto r = make().prove("p");
  EXPECT_EQ(inner.calls, 1);
  EXPECT_TRUE(r.has_value());
}

TEST_F(CachingProverTest, TimeoutIsReusedWhenTheNewLimitIsNotHigher) {
  inner.answer = std::unexpected(ProverError{"cbmc timed out after 30 s", true});
  (void)make().prove("slow");
  (void)make().prove("slow");  // samma gräns
  inner.timeout = 10;
  auto c = make();
  auto r = c.prove("slow");  // lägre gräns
  EXPECT_EQ(inner.calls, 1);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().timedOut);
  EXPECT_EQ(c.timeouts(), 1u);
}

TEST_F(CachingProverTest, TimeoutIsProvedAgainWithAHigherLimitAndTheAnswerIsKept) {
  inner.timeout = 10;
  inner.answer = std::unexpected(ProverError{"cbmc timed out after 10 s", true});
  (void)make().prove("slow");
  inner.timeout = 600;
  inner.answer = std::vector<ProofFailure>{};
  auto r = make().prove("slow");
  EXPECT_EQ(inner.calls, 2);
  EXPECT_TRUE(r.has_value());
  inner.timeout = 10;  // nästa snabba pass: svaret är sparat
  auto again = make();
  EXPECT_TRUE(again.prove("slow").has_value());
  EXPECT_EQ(inner.calls, 2);
  EXPECT_EQ(again.timeouts(), 0u);
}

TEST_F(CachingProverTest, TimeoutIsProvedAgainWithoutALimit) {
  inner.answer = std::unexpected(ProverError{"cbmc timed out after 30 s", true});
  (void)make().prove("slow");
  inner.timeout = 0;
  (void)make().prove("slow");
  EXPECT_EQ(inner.calls, 2);
}

TEST_F(CachingProverTest, NewTimeoutsAreCounted) {
  inner.answer = std::unexpected(ProverError{"cbmc timed out after 30 s", true});
  auto c = make();
  (void)c.prove("a");
  (void)c.prove("b");
  EXPECT_EQ(c.timeouts(), 2u);
}
