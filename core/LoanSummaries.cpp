#include "LoanSummaries.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace pork {

namespace {

std::vector<std::string> words(std::string_view line) {
  std::vector<std::string> out;
  std::istringstream in{std::string(line)};
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

bool matches(LoanSummaries::Shape shape, const std::vector<bool>& it) {
  if (it.size() < shape.iterators || (!shape.more && it.size() != shape.iterators)) return false;
  return std::all_of(it.begin(), it.begin() + shape.iterators, [](bool b) { return b; });
}

// Mer specifik först: exakt form före öppen, sedan fler iteratorer.
bool moreSpecific(LoanSummaries::Shape a, LoanSummaries::Shape b) {
  if (a.more != b.more) return !a.more;
  return a.iterators > b.iterators;
}

// `erase(iterator)` -> ("erase", {1, false}); `f(iterator,...)` -> ("f", {1, true}).
// `operator()` har ingen form.
std::expected<std::pair<std::string, LoanSummaries::Shape>, std::string> splitShape(const std::string& w) {
  auto open = w.find('(');
  if (!w.ends_with(")") || w.ends_with("operator()") || open == std::string::npos || open == 0)
    return std::pair{w, LoanSummaries::Shape{}};
  LoanSummaries::Shape shape{0, false};
  std::string items = w.substr(open + 1, w.size() - open - 2);
  std::istringstream in(items);
  std::vector<std::string> parts;
  for (std::string part; std::getline(in, part, ',');) parts.push_back(part);
  for (std::size_t k = 0; k < parts.size(); ++k) {
    if (parts[k] == "iterator" && !shape.more) {
      ++shape.iterators;
    } else if (parts[k] == "..." && k + 1 == parts.size() && shape.iterators > 0) {
      shape.more = true;
    } else {
      return std::unexpected("unknown parameter shape in '" + w + "' (iterator, ..., ending with '...')");
    }
  }
  if (shape.iterators == 0) return std::unexpected("unknown parameter shape in '" + w + "'");
  return std::pair{w.substr(0, open), shape};
}

}  // namespace

std::optional<LoanRule> LoanSummaries::find(const Query& q) const {
  std::vector<std::string> names = {q.qualified, q.name};
  if (q.isConst) names.push_back("<const>");
  for (const std::string& n : names) {
    if (n.empty()) continue;
    const LoanRule* best = nullptr;
    Shape bestShape;
    for (auto it = rules_.lower_bound({q.category, n, Shape{0, false}});
         it != rules_.end() && std::get<0>(it->first) == q.category && std::get<1>(it->first) == n; ++it) {
      const Shape s = std::get<2>(it->first);
      if (!matches(s, q.iteratorParams)) continue;
      if (best == nullptr || moreSpecific(s, bestShape)) {
        best = &it->second;
        bestShape = s;
      }
    }
    if (best != nullptr) return *best;
  }
  return std::nullopt;
}

std::expected<void, std::string> LoanSummaries::parse(std::string_view text, const std::string& source) {
  static const std::map<std::string, Category> categories = {
      {"owner", Category::owner}, {"pointer", Category::pointer}, {"free", Category::free}};
  static const std::map<std::string, LoanEffect> effects = {
      {"default", LoanEffect::standard}, {"reads", LoanEffect::reads},       {"access", LoanEffect::access},
      {"self", LoanEffect::self},        {"writeAccess", LoanEffect::writeAccess},
      {"identity", LoanEffect::identity}, {"insert", LoanEffect::insert}};
  std::set<Key> seen;
  std::istringstream in{std::string(text)};
  unsigned lineNo = 0;
  for (std::string line; std::getline(in, line);) {
    ++lineNo;
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    std::vector<std::string> w = words(line);
    if (w.empty()) continue;
    std::string where = source + ":" + std::to_string(lineNo);
    auto fail = [&](const std::string& why) { return std::unexpected(where + ": " + why); };

    auto cat = categories.find(w[0]);
    if (cat == categories.end()) return fail("unknown category '" + w[0] + "' (owner, pointer, free)");
    auto colon = std::find(w.begin(), w.end(), ":");
    if (colon == w.end()) return fail("missing ':' before the effect");
    if (colon == w.begin() + 1) return fail("no names before ':'");
    if (colon + 1 == w.end()) return fail("no effect after ':'");
    auto eff = effects.find(*(colon + 1));
    if (eff == effects.end()) return fail("unknown effect '" + *(colon + 1) + "'");
    LoanRule rule{eff->second, false, false, false, false, where, {}};
    for (auto m = colon + 2; m != w.end(); ++m) {
      if (*m == "position") rule.position = true;
      else if (*m == "range") rule.range = true;
      else if (*m == "consumes") rule.consumes = true;
      else if (*m == "forwards") rule.forwards = true;
      else if (*m == "copies") rule.copies = true;
      else if (unsigned i = 0, j = 0; std::sscanf(m->c_str(), "interval(%u,%u)", &i, &j) == 2 &&
                                      *m == "interval(" + std::to_string(i) + "," + std::to_string(j) + ")" && i < j)
        rule.intervals.emplace_back(i, j);
      else return fail("unknown modifier '" + *m + "' (position, range, consumes, forwards, copies, interval(i,j) with i < j)");
    }
    for (auto n = w.begin() + 1; n != colon; ++n) {
      auto split = splitShape(*n);
      if (!split) return fail(split.error());
      Key key{cat->second, split->first, split->second};
      if (!seen.insert(key).second) return fail("'" + *n + "' is listed twice for " + w[0]);
      rules_[key] = rule;
    }
  }
  return {};
}

std::expected<LoanSummaries, std::string> loadLoanSummaries(const std::string& extraFile) {
  LoanSummaries s;
  if (auto r = s.parse(stdLoanSummariesText(), "summaries/std.txt"); !r) return std::unexpected(r.error());
  if (extraFile.empty()) return s;
  std::ifstream in(extraFile);
  if (!in) return std::unexpected("cannot read summaries file " + extraFile);
  std::stringstream text;
  text << in.rdbuf();
  if (auto r = s.parse(text.str(), extraFile); !r) return std::unexpected(r.error());
  return s;
}

}  // namespace pork
