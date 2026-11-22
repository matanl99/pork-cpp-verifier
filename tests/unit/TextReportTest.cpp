#include "TextReport.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace {

pork::FunctionFacts fact() {
  pork::FunctionFacts f;
  f.name = "ns::f";
  f.file = "a.cpp";
  f.line = 3;
  f.column = 5;
  return f;
}

struct Printed {
  std::string out;
  std::string err;
};

Printed functionLine(const pork::Verdict& v) {
  std::ostringstream out, err;
  pork::TextReport(out, err).function(fact(), v);
  return {out.str(), err.str()};
}

}  // namespace

TEST(TextReport, Stone) {
  EXPECT_EQ(functionLine(pork::Verdict::stone()).out, "a.cpp:3:5: ns::f: stone\n");
}

TEST(TextReport, StoneWithAssumptionsSorted) {
  auto v = pork::Verdict::stone();
  v.assumptions = {"std::fabs", "hypot"};
  EXPECT_EQ(functionLine(v).out, "a.cpp:3:5: ns::f: stone: assuming hypot, std::fabs\n");
}

TEST(TextReport, Trusted) {
  EXPECT_EQ(functionLine(pork::Verdict::trust()).out, "a.cpp:3:5: ns::f: trusted\n");
}

TEST(TextReport, NotStone) {
  EXPECT_EQ(functionLine(pork::Verdict::notStone("uses global")).out,
            "a.cpp:3:5: ns::f: not-stone: uses global\n");
}

pork::FunctionFacts required() {
  pork::FunctionFacts f = fact();
  f.required = true;
  return f;
}

TEST(TextReport, ViolationGoesToErrInCompilerFormat) {
  std::ostringstream out, err;
  pork::TextReport(out, err).violation(required(), pork::Verdict::notStone("uses global"));
  EXPECT_EQ(out.str(), "");
  EXPECT_EQ(err.str(),
            "a.cpp:3:5: error: 'ns::f' is marked pork::stone but is not-stone: uses global\n");
}

TEST(TextReport, ViolationForTrustedSaysBodyNotAnalysed) {
  std::ostringstream out, err;
  pork::TextReport(out, err).violation(required(), pork::Verdict::trust());
  EXPECT_EQ(err.str(),
            "a.cpp:3:5: error: 'ns::f' is marked pork::stone but is trusted (body not analysed)\n");
}

// BRZ-6: ett krav i en funktion som inte är märkt pork::stone ger bara
// PORK_DEPENDS-felet.
TEST(TextReport, FlowRequirementViolationWithoutStoneMark) {
  std::ostringstream out, err;
  auto v = pork::Verdict::notStone("uses global");
  v.bronze = "the function is not Stone";
  pork::TextReport(out, err).violation(fact(), v);
  EXPECT_EQ(err.str(), "a.cpp:3:5: error: 'ns::f' does not meet PORK_DEPENDS: the function is not Stone\n");
}

TEST(TextReport, StrayRequirementGoesToErrInCompilerFormat) {
  std::ostringstream out, err;
  pork::TextReport(out, err).strayRequirement({"a.cpp", 4, 12, "the initializer of field 'Cfg::b'"});
  EXPECT_EQ(err.str(), "a.cpp:4:12: error: PORK_DEPENDS in the initializer of field 'Cfg::b' is outside a function body\n");
}

TEST(TextReport, Silver) {
  auto v = pork::Verdict::stone();
  v.kind = pork::Verdict::Kind::silver;
  v.assumptions = {"std::hypot"};
  EXPECT_EQ(functionLine(v).out, "a.cpp:3:5: ns::f: silver: assuming std::hypot\n");
}

TEST(TextReport, StoneNotSilverPutsAssumptionsLast) {
  auto v = pork::Verdict::stone();
  v.reason = "division by zero in a / b at x.cpp:4";
  v.assumptions = {"std::hypot"};
  EXPECT_EQ(functionLine(v).out,
            "a.cpp:3:5: ns::f: stone: not silver: division by zero in a / b at x.cpp:4; "
            "assuming std::hypot\n");
}
