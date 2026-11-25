#include "LoanSummaries.hpp"

#include <gtest/gtest.h>

namespace pork {
namespace {

using Cat = LoanSummaries::Category;

LoanSummaries builtin() {
  auto s = loadLoanSummaries();
  EXPECT_TRUE(s.has_value()) << (s ? "" : s.error());
  return s ? *s : LoanSummaries{};
}

std::optional<LoanRule> find(const LoanSummaries& s, Cat c, std::string qualified, std::string name,
                             std::vector<bool> it = {}, bool isConst = false) {
  return s.find({c, std::move(qualified), std::move(name), std::move(it), isConst});
}

TEST(LoanSummariesTest, BuiltinFileParses) { EXPECT_GT(builtin().size(), 100u); }

TEST(LoanSummariesTest, QualifiedNameBeatsName) {
  LoanSummaries s = builtin();
  EXPECT_EQ(find(s, Cat::owner, "std::map::operator[]", "operator[]")->effect, LoanEffect::insert);
  EXPECT_EQ(find(s, Cat::owner, "std::unordered_map::operator[]", "operator[]")->effect, LoanEffect::writeAccess);
  EXPECT_EQ(find(s, Cat::owner, "std::vector::operator[]", "operator[]")->effect, LoanEffect::access);
}

TEST(LoanSummariesTest, NodeContainersKeepElementsOnInsert) {
  LoanSummaries s = builtin();
  EXPECT_EQ(find(s, Cat::owner, "std::map::insert", "insert", {false})->effect, LoanEffect::insert);
  EXPECT_EQ(find(s, Cat::owner, "std::list::push_back", "push_back", {false})->effect, LoanEffect::insert);
  // Omallokering/rehash: standardeffekten (vector::push_back har bara copies).
  EXPECT_EQ(find(s, Cat::owner, "std::vector::push_back", "push_back", {false})->effect, LoanEffect::standard);
  EXPECT_TRUE(find(s, Cat::owner, "std::vector::push_back", "push_back", {false})->copies);
  EXPECT_FALSE(find(s, Cat::owner, "std::unordered_map::insert", "insert", {false}));
}

// get<I>(t&&) ger en referens in i t; flytten sker där resultatet används.
TEST(LoanSummariesTest, GetForwards) {
  LoanSummaries s = builtin();
  auto get = find(s, Cat::free, "std::get", "get", {});
  ASSERT_TRUE(get);
  EXPECT_TRUE(get->forwards);
  EXPECT_EQ(get->effect, LoanEffect::access);
  auto getIf = find(s, Cat::free, "std::get_if", "get_if", {});
  ASSERT_TRUE(getIf);
  EXPECT_FALSE(getIf->forwards);
}

TEST(LoanSummariesTest, ShapeSelectsOverload) {
  LoanSummaries s = builtin();
  auto one = find(s, Cat::owner, "std::map::erase", "erase", {true});
  ASSERT_TRUE(one);
  EXPECT_TRUE(one->consumes);
  EXPECT_TRUE(one->position);
  auto pair = find(s, Cat::owner, "std::vector::erase", "erase", {true, true});
  ASSERT_TRUE(pair);
  EXPECT_TRUE(pair->range);
  EXPECT_FALSE(pair->consumes);
  EXPECT_FALSE(find(s, Cat::owner, "std::map::erase", "erase", {false}));  // erase(key)
  // insert(p, i, j): bara positionen får peka in i behållaren.
  auto hint = find(s, Cat::owner, "std::vector::insert", "insert", {true, true, true});
  ASSERT_TRUE(hint);
  EXPECT_TRUE(hint->position);
  EXPECT_FALSE(hint->range);
  auto mapHint = find(s, Cat::owner, "std::map::insert", "insert", {true, false});
  EXPECT_EQ(mapHint->effect, LoanEffect::insert);
  EXPECT_TRUE(mapHint->position);
}

// Binär ranges::transform har två intervall; std::transform med fyra
// iteratorer (first2 ensam) och den unära har ett (pork_examples_2/05).
TEST(LoanSummariesTest, LongerShapeIsMoreSpecific) {
  LoanSummaries s = builtin();
  auto ranges2 = find(s, Cat::free, "std::ranges::transform", "transform", {true, true, true, true, true, false});
  ASSERT_TRUE(ranges2);
  EXPECT_EQ(ranges2->intervals, (std::vector<std::pair<unsigned, unsigned>>{{0, 1}, {2, 3}}));
  auto std2 = find(s, Cat::free, "std::transform", "transform", {true, true, true, true, false});
  ASSERT_TRUE(std2);
  EXPECT_EQ(std2->intervals, (std::vector<std::pair<unsigned, unsigned>>{{0, 1}}));
  auto unary = find(s, Cat::free, "std::ranges::transform", "transform", {true, true, true, false, false});
  ASSERT_TRUE(unary);
  EXPECT_EQ(unary->intervals, (std::vector<std::pair<unsigned, unsigned>>{{0, 1}}));
}

TEST(LoanSummariesTest, ConstFallbackOnlyForConstMethods) {
  LoanSummaries s = builtin();
  EXPECT_EQ(find(s, Cat::pointer, "std::span::size", "size", {}, true)->effect, LoanEffect::reads);
  EXPECT_FALSE(find(s, Cat::pointer, "std::span::size", "size", {}, false));
}

TEST(LoanSummariesTest, LaterFileOverrides) {
  LoanSummaries s = builtin();
  ASSERT_TRUE(s.parse("owner std::vector::push_back : reads\n", "extra"));
  EXPECT_EQ(find(s, Cat::owner, "std::vector::push_back", "push_back", {false})->effect, LoanEffect::reads);
  EXPECT_EQ(find(s, Cat::owner, "std::vector::push_back", "push_back", {false})->origin, "extra:1");
}

TEST(LoanSummariesTest, Intervals) {
  LoanSummaries s;
  ASSERT_TRUE(s.parse("free rotate : default interval(0,1) interval(1,2) interval(0,2)\n", "f"));
  auto r = find(s, Cat::free, "std::rotate", "rotate", {true, true, true});
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->intervals, (std::vector<std::pair<unsigned, unsigned>>{{0, 1}, {1, 2}, {0, 2}}));
}

TEST(LoanSummariesTest, ResultIn) {
  // [alg.remove]: resultatet är en position i intervallet (STN-85).
  auto r = builtin().find({LoanSummaries::Category::free, "std::remove_if", "remove_if", {true, true, false}});
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->resultIn, std::optional<unsigned>(0));
  auto sort = builtin().find({LoanSummaries::Category::free, "std::sort", "sort", {true, true}});
  ASSERT_TRUE(sort.has_value());
  EXPECT_FALSE(sort->resultIn.has_value());
  LoanSummaries s;
  EXPECT_EQ(s.parse("free f : default resultIn(x)\n", "f").error(),
            "f:1: unknown modifier 'resultIn(x)' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i))");
  EXPECT_EQ(s.parse("free f : default resultIn(1)y\n", "f").error(),
            "f:1: unknown modifier 'resultIn(1)y' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i))");
}

TEST(LoanSummariesTest, Errors) {
  LoanSummaries s;
  EXPECT_EQ(s.parse("owner size reads\n", "f").error(), "f:1: missing ':' before the effect");
  EXPECT_EQ(s.parse("tree size : reads\n", "f").error(), "f:1: unknown category 'tree' (owner, pointer, free)");
  EXPECT_EQ(s.parse("owner size : read\n", "f").error(), "f:1: unknown effect 'read'");
  EXPECT_EQ(s.parse("owner size : reads fast\n", "f").error(),
            "f:1: unknown modifier 'fast' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i))");
  EXPECT_EQ(s.parse("free sort : default interval(1,0)\n", "f").error(),
            "f:1: unknown modifier 'interval(1,0)' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i))");
  EXPECT_EQ(s.parse("free sort : default interval(0, 1)\n", "f").error(),
            "f:1: unknown modifier 'interval(0,' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i))");
  EXPECT_EQ(s.parse("free sort : default interval(0,1)x\n", "f").error(),
            "f:1: unknown modifier 'interval(0,1)x' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i))");
  EXPECT_EQ(s.parse("# c\nowner size size : reads\n", "f").error(), "f:2: 'size' is listed twice for owner");
  EXPECT_EQ(s.parse("owner erase(key) : reads\n", "f").error(),
            "f:1: unknown parameter shape in 'erase(key)' (iterator, ..., ending with '...')");
  EXPECT_FALSE(s.parse("free f(...,iterator) : reads\n", "f"));
  EXPECT_TRUE(s.parse("free operator() : reads\n", "f"));
}

}  // namespace
}  // namespace pork
