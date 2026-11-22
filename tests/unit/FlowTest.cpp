#include "Flow.hpp"

#include <gtest/gtest.h>

namespace pork {
namespace {

using V = FlowValue;
using Deps = std::set<std::string>;

FlowStmt assign(std::string t, FlowValues v) { return {FlowStmt::Kind::assign, std::move(t), std::move(v), {}}; }
FlowStmt ret(FlowValues v) { return {FlowStmt::Kind::ret, {}, std::move(v), {}}; }
FlowStmt call(FlowCall c) { return {FlowStmt::Kind::call, {}, {}, std::move(c)}; }

// Block 0 = ingång, sista = utgång.
FlowGraph graph(std::vector<FlowBlock> b, std::vector<std::string> params, std::vector<bool> outputs,
                bool returns) {
  FlowGraph g;
  g.blocks = std::move(b);
  g.entry = 0;
  g.exit = static_cast<unsigned>(g.blocks.size() - 1);
  g.params = std::move(params);
  g.outputs = std::move(outputs);
  g.returnsValue = returns;
  return g;
}

// int quote(int price, bool discounted, int& audit) {
//   audit = price; if (discounted) return price / 2; return price; }
FlowGraph quote(bool flagInAudit) {
  FlowValues rhs{V::read("price")};
  if (flagInAudit) rhs.push_back(V::read("discounted"));
  return graph({{{assign("audit", rhs)}, {V::read("discounted")}, {1, 2}},
                {{ret({V::read("price")})}, {}, {3}},
                {{ret({V::read("price")})}, {}, {3}},
                {}},
               {"price", "discounted", "audit"}, {false, false, true}, true);
}

TEST(FlowTest, ReturnAndAlwaysWrittenOutput) {
  FlowSummary s = summarize(quote(false), {});
  ASSERT_TRUE(s.known);
  EXPECT_EQ(s.deps.at("return"), (Deps{"discounted", "price"}));
  EXPECT_EQ(s.deps.at("audit"), (Deps{"price"}));
  EXPECT_TRUE(s.always.contains("audit"));
  EXPECT_EQ(flowLines(s), (std::vector<std::string>{"return <- discounted, price",
                                                   "audit <- price (always written)"}));
}

TEST(FlowTest, OnlyTheChangedOutputGetsTheNewDependency) {
  FlowSummary s = summarize(quote(true), {});
  EXPECT_EQ(s.deps.at("audit"), (Deps{"discounted", "price"}));
  EXPECT_EQ(s.deps.at("return"), (Deps{"discounted", "price"}));
}

// void audit_if(int price, bool enabled, int& audit) { if (enabled) audit = price; }
TEST(FlowTest, ConditionalWriteKeepsTheOldValue) {
  FlowGraph g = graph({{{}, {V::read("enabled")}, {1, 2}}, {{assign("audit", {V::read("price")})}, {}, {2}}, {}},
                      {"price", "enabled", "audit"}, {false, false, true}, false);
  FlowSummary s = summarize(g, {});
  EXPECT_EQ(s.deps.at("audit"), (Deps{"audit", "enabled", "price"}));
  EXPECT_FALSE(s.always.contains("audit"));
}

// void choose(bool flag, int a, int b, int& output) { if (flag) { output = a; return; } output = b; }
TEST(FlowTest, EarlyReturnStillAlwaysWrites) {
  FlowGraph g = graph({{{}, {V::read("flag")}, {1, 2}},
                       {{assign("output", {V::read("a")})}, {}, {3}},
                       {{assign("output", {V::read("b")})}, {}, {3}},
                       {}},
                      {"flag", "a", "b", "output"}, {false, false, false, true}, false);
  FlowSummary s = summarize(g, {});
  EXPECT_EQ(s.deps.at("output"), (Deps{"a", "b", "flag"}));
  EXPECT_TRUE(s.always.contains("output"));
}

// void write_in_loop(int count, int value, int& output) {
//   for (int i = 0; i < count; ++i) output = value; }
TEST(FlowTest, LoopMayNotRun) {
  FlowGraph g = graph({{{assign("i", {})}, {}, {1}},
                       {{}, {V::read("i"), V::read("count")}, {2, 3}},
                       {{assign("output", {V::read("value")}), assign("i", {V::read("i")})}, {}, {1}},
                       {}},
                      {"count", "value", "output"}, {false, false, true}, false);
  FlowSummary s = summarize(g, {});
  EXPECT_EQ(s.deps.at("output"), (Deps{"count", "output", "value"}));
  EXPECT_FALSE(s.always.contains("output"));
}

// int select_first(int selected, int unused) { return selected; }
// int reordered(int left, int right) { return select_first(right, left); }
TEST(FlowTest, CallMapsParametersToArguments) {
  std::map<std::string, FlowGraph> graphs;
  graphs["sel"] = graph({{{ret({V::read("selected")})}, {}, {1}}, {}}, {"selected", "unused"}, {false, false}, true);
  FlowCall c{1, "sel", "select_first", {{V::read("right")}, {V::read("left")}}, {"", ""}};
  graphs["re"] = graph({{{call(c), ret({V::result(1)})}, {}, {1}}, {}}, {"left", "right"}, {false, false}, true);
  auto s = computeFlows(graphs);
  EXPECT_EQ(s.at("re").deps.at("return"), (Deps{"right"}));
}

// void set_value(int& out, int value) { out = value; }
// void forward_write(int value, int& out) { set_value(out, value); }
TEST(FlowTest, WriteThroughCallIsAlwaysWhenTheCalleeAlwaysWrites) {
  std::map<std::string, FlowGraph> graphs;
  graphs["set"] = graph({{{assign("out", {V::read("value")})}, {}, {1}}, {}}, {"out", "value"}, {true, false}, false);
  FlowCall c{1, "set", "set_value", {{V::read("out")}, {V::read("value")}}, {"out", ""}};
  graphs["fwd"] = graph({{{call(c)}, {}, {1}}, {}}, {"value", "out"}, {false, true}, false);
  auto s = computeFlows(graphs);
  EXPECT_EQ(s.at("fwd").deps.at("out"), (Deps{"value"}));
  EXPECT_TRUE(s.at("fwd").always.contains("out"));
}

// Okänd kod: allt beror på allt, och skrivningen är inte säker.
TEST(FlowTest, UnknownCalleeIsConservative) {
  FlowCall c{1, "", "fill_audit", {{V::read("price")}, {V::read("audit")}}, {"", "audit"}};
  FlowGraph g = graph({{{call(c), ret({V::read("price")})}, {}, {1}}, {}}, {"price", "audit"}, {false, true}, true);
  FlowSummary s = summarize(g, {});
  EXPECT_EQ(s.deps.at("audit"), (Deps{"audit", "price"}));
  EXPECT_FALSE(s.always.contains("audit"));
  EXPECT_EQ(s.deps.at("return"), (Deps{"price"}));
}

// Fält: en skrivning till result.total lämnar result.stamp orörd.
TEST(FlowTest, FieldsAreSeparate) {
  FlowGraph g = graph({{{assign("result.total", {V::read("total")})}, {}, {1}}, {}}, {"result", "total"},
                      {true, false}, false);
  FlowSummary s = summarize(g, {});
  EXPECT_EQ(s.deps.size(), 1u);
  EXPECT_EQ(s.deps.at("result.total"), (Deps{"total"}));
}

// Rekursion: fixpunkten tar med beroendet genom anropet.
TEST(FlowTest, RecursionReachesFixpoint) {
  std::map<std::string, FlowGraph> graphs;
  FlowCall c{1, "f", "f", {{V::read("b")}, {V::read("a")}}, {"", ""}};
  graphs["f"] = graph({{{}, {V::read("a")}, {1, 2}}, {{ret({V::read("a")})}, {}, {3}},
                       {{call(c), ret({V::result(1)})}, {}, {3}}, {}},
                      {"a", "b"}, {false, false}, true);
  auto s = computeFlows(graphs);
  EXPECT_EQ(s.at("f").deps.at("return"), (Deps{"a", "b"}));
}

}  // namespace
}  // namespace pork
