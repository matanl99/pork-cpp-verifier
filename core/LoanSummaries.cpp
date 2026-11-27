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

using Param = LoanSummaries::Param;

bool hasRange(const std::vector<Param>& ps) { return std::ranges::find(ps, Param::range) != ps.end(); }

bool matches(const LoanSummaries::Shape& shape, const std::vector<Param>& ps) {
  if (hasRange(ps) && !hasRange(shape.params)) return false;
  if (ps.size() < shape.params.size() || (!shape.more && ps.size() != shape.params.size())) return false;
  return std::equal(shape.params.begin(), shape.params.end(), ps.begin());
}

// Mer specifik först: exakt form före öppen, sedan fler parametrar.
bool moreSpecific(const LoanSummaries::Shape& a, const LoanSummaries::Shape& b) {
  if (a.more != b.more) return !a.more;
  return a.params.size() > b.params.size();
}

// `erase(iterator)` -> ("erase", {{iterator}, false}); `f(range,...)` -> ("f", {{range}, true}).
// `operator()` har ingen form.
std::expected<std::pair<std::string, LoanSummaries::Shape>, std::string> splitShape(const std::string& w) {
  auto open = w.find('(');
  if (!w.ends_with(")") || w.ends_with("operator()") || open == std::string::npos || open == 0)
    return std::pair{w, LoanSummaries::Shape{}};
  LoanSummaries::Shape shape{{}, false};
  std::string items = w.substr(open + 1, w.size() - open - 2);
  std::istringstream in(items);
  std::vector<std::string> parts;
  for (std::string part; std::getline(in, part, ',');) parts.push_back(part);
  for (std::size_t k = 0; k < parts.size(); ++k) {
    if ((parts[k] == "iterator" || parts[k] == "range") && !shape.more) {
      shape.params.push_back(parts[k] == "range" ? Param::range : Param::iterator);
    } else if (parts[k] == "..." && k + 1 == parts.size() && !shape.params.empty()) {
      shape.more = true;
    } else {
      return std::unexpected("unknown parameter shape in '" + w + "' (iterator, range, ..., ending with '...')");
    }
  }
  if (shape.params.empty()) return std::unexpected("unknown parameter shape in '" + w + "'");
  return std::pair{w.substr(0, open), shape};
}

}  // namespace

std::optional<LoanRule> LoanSummaries::find(const Query& q) const {
  std::vector<std::string> names = {q.qualified, q.name};
  if (q.isConst) names.push_back("<const>");
  if (q.isConstructor) names.push_back("<ctor>");
  for (const std::string& n : names) {
    if (n.empty()) continue;
    const LoanRule* best = nullptr;
    Shape bestShape;
    for (auto it = rules_.lower_bound({q.category, n, Shape{{}, false}});
         it != rules_.end() && std::get<0>(it->first) == q.category && std::get<1>(it->first) == n; ++it) {
      const Shape& s = std::get<2>(it->first);
      if (!matches(s, q.params)) continue;
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
    LoanRule rule{eff->second, false, false, false, false, where, {}, false, std::nullopt};
    for (auto m = colon + 2; m != w.end(); ++m) {
      if (*m == "position") rule.position = true;
      else if (*m == "range") rule.range = true;
      else if (*m == "consumes") rule.consumes = true;
      else if (*m == "forwards") rule.forwards = true;
      else if (*m == "copies") rule.copies = true;
      else if (*m == "nonEmpty") rule.nonEmpty = true;
      else if (*m == "resultNonEmpty") rule.resultNonEmpty = true;
      else if (*m == "resultNullIfEmpty") rule.resultNullIfEmpty = true;
      else if (*m == "keepsElements") rule.keepsElements = true;
      else if (*m == "insertsArguments") rule.insertsArguments = true;
      else if (*m == "swaps") rule.swaps = true;
      else if (*m == "keepsValues") rule.keepsValues = true;
      else if (*m == "invalidatesIterators") rule.invalidatesIterators = true;
      else if (*m == "resets") rule.resets = true;
      else if (unsigned i = 0, j = 0; std::sscanf(m->c_str(), "interval(%u,%u)", &i, &j) == 2 &&
                                      *m == "interval(" + std::to_string(i) + "," + std::to_string(j) + ")" && i < j)
        rule.intervals.emplace_back(i, j);
      else if (unsigned i = 0; std::sscanf(m->c_str(), "resultIn(%u)", &i) == 1 &&
                               *m == "resultIn(" + std::to_string(i) + ")")
        rule.resultIn = i;
      else return fail("unknown modifier '" + *m +
                       "' (position, range, consumes, forwards, copies, interval(i,j) with i < j, resultIn(i), "
                       "nonEmpty, resultNonEmpty, resultNullIfEmpty, keepsElements, insertsArguments, swaps, keepsValues, invalidatesIterators, resets)");
    }
    if (rule.invalidatesIterators && rule.effect != LoanEffect::insert)
      return fail("invalidatesIterators needs the effect insert");
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
