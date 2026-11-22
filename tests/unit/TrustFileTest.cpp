#include "TrustFile.hpp"

#include <gtest/gtest.h>

#include <sstream>

TEST(TrustFile, OneNamePerLineWithCommentsAndWhitespace) {
  std::istringstream in(
      "# betrodda\n"
      "std::fabs\n"
      "\n"
      "  hypot  # libm\n"
      "\tstd::clamp\r\n");
  EXPECT_EQ(pork::parseTrustList(in).names,
            (std::set<std::string>{"std::fabs", "hypot", "std::clamp"}));
}

TEST(TrustFile, TypeLinesGoToTypes) {
  std::istringstream in(
      "type std::optional\n"
      "type   std::variant  # flera blanksteg\n"
      "std::optional::*\n");
  auto list = pork::parseTrustList(in);
  EXPECT_EQ(list.types, (std::set<std::string>{"std::optional", "std::variant"}));
  EXPECT_EQ(list.names, (std::set<std::string>{"std::optional::*"}));
}

TEST(TrustFile, MissingFileIsError) {
  auto r = pork::loadTrustList("/nonexistent/pork-trust.txt");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error(), "cannot read trust file '/nonexistent/pork-trust.txt'");
}
