#include "Flow.hpp"

#include <algorithm>
#include <deque>

namespace pork {

namespace {

using Deps = std::set<std::string>;

bool within(const std::string& p, const std::string& a) {  // p ligger i a (eller är a)
  return p == a || (p.size() > a.size() && p.starts_with(a) && (p[a.size()] == '.' || p[a.size()] == '['));
}

// Platserna i `vals` som ligger i p (p själv och p.x, p[*] ...): ett
// sammanhängande intervall i den sorterade mappen, från lower_bound(p) så länge
// nyckeln börjar med p (p2 och pa börjar också med p men ligger inte i p).
template <class Map, class F>
void forEachWithin(Map& vals, const std::string& p, F&& f) {
  for (auto it = vals.lower_bound(p); it != vals.end() && it->first.starts_with(p);) {
    auto next = std::next(it);
    if (within(it->first, p)) f(it);
    it = next;
  }
}

std::string rootOf(const std::string& p) {
  return p.substr(0, std::min(p.find('.'), p.find('[')));
}

// Ett tillstånd på en väg: platsernas innehåll (som mängder av ingångsvärden),
// vad som kan ha skrivits och vad som säkert har skrivits.
struct State {
  bool reached = false;
  std::map<std::string, Deps> vals;
  std::set<std::string> written;
  std::set<std::string> always;
  bool operator==(const State&) const = default;
};

class Analysis {
 public:
  Analysis(const FlowGraph& g, const std::map<std::string, FlowSummary>& callees, bool markers = false)
      : g_(g), callees_(callees), inputs_(g.params.begin(), g.params.end()), markers_(markers) {}

  // BRZ-7: markörerna ("§b.i" för sats i i block b, "§cb" för villkoret i b)
  // som inte når någon utdata, som anteckningar. Körs med markers = true.
  std::vector<FlowNote> noEffect() const { return noEffect_; }

  FlowSummary run() {
    const std::size_t n = g_.blocks.size();
    postDominators();
    std::vector<Deps> ctl(n), condDeps(n);
    std::vector<State> out(n);
    for (int round = 0; round < 100; ++round) {
      Deps ret;
      out.assign(n, State{});
      std::deque<unsigned> work{g_.entry};
      std::vector<State> in(n);
      in[g_.entry].reached = true;
      while (!work.empty()) {
        unsigned b = work.front();
        work.pop_front();
        State s = in[b];
        transfer(s, g_.blocks[b], ctl[b], ret);
        if (s == out[b] && out[b].reached) continue;
        out[b] = s;
        for (unsigned succ : g_.blocks[b].succs) {
          State merged = merge(in[succ], s);
          if (!(merged == in[succ]) || !out[succ].reached) {
            in[succ] = std::move(merged);
            if (std::find(work.begin(), work.end(), succ) == work.end()) work.push_back(succ);
          }
        }
      }
      // Styrberoenden: villkoren som en block beror på, transitivt.
      std::vector<Deps> nextCond(n), nextCtl(n);
      for (unsigned b = 0; b < n; ++b)
        if (out[b].reached) {
          nextCond[b] = resolve(out[b], g_.blocks[b].cond);
          if (markers_ && g_.blocks[b].succs.size() > 1) nextCond[b].insert(condMarker(b));
        }
      for (bool changed = true; changed;) {
        changed = false;
        for (unsigned b = 0; b < n; ++b)
          for (unsigned a : cd_[b]) {
            std::size_t before = nextCtl[b].size();
            nextCtl[b].insert(nextCond[a].begin(), nextCond[a].end());
            nextCtl[b].insert(nextCtl[a].begin(), nextCtl[a].end());
            changed |= nextCtl[b].size() != before;
          }
      }
      if (nextCtl == ctl) {
        FlowSummary s = summary(in[g_.exit], ret);
        // Ett kast: ett block som nås men inte leder någonstans (inte utgången).
        for (unsigned b = 0; b < n; ++b)
          if (out[b].reached && b != g_.exit && g_.blocks[b].succs.empty()) {
            s.external = true;
            sink_.insert(ctl[b].begin(), ctl[b].end());
            // En anropare som fångar undantaget ser utdata som de är vid kastet.
            for (std::size_t i = 0; i < g_.params.size(); ++i)
              if (i < g_.outputs.size() && g_.outputs[i])
                for (const std::string& p : out[b].written)
                  if (rootOf(p) == g_.params[i]) {
                    Deps d = read(out[b], p);
                    sink_.insert(d.begin(), d.end());
                  }
          }
        if (sinkExternal_) s.external = true;
        if (markers_) collectNoEffect(out, s, ret);
        return s;
      }
      ctl = std::move(nextCtl);
      condDeps = std::move(nextCond);
    }
    FlowSummary s;
    s.why = "flow did not converge";
    return s;
  }

 private:
  // Innehållet i p: närmaste skrivna omslutande plats, annars ingångsvärdet
  // (lokaler har inget), plus det som skrivits inuti p.
  Deps read(const State& s, const std::string& p) const {
    Deps d;
    // Den närmaste omslutande platsen: p själv eller det längsta prefixet av p
    // som slutar före en '.' eller '['.
    auto best = s.vals.find(p);
    for (std::size_t i = p.size(); best == s.vals.end() && i-- > 0;)
      if (p[i] == '.' || p[i] == '[') best = s.vals.find(p.substr(0, i));
    if (best != s.vals.end()) d = best->second;
    else if (inputs_.contains(rootOf(p))) d.insert(p);
    forEachWithin(s.vals, p, [&](auto it) {
      if (it->first != p) d.insert(it->second.begin(), it->second.end());
    });
    return d;
  }

  Deps resolve(const State& s, const FlowValues& vs) const {
    Deps d;
    for (const FlowValue& v : vs) {
      Deps x = read(s, v.kind == FlowValue::Kind::place ? v.place : "#" + std::to_string(v.call));
      d.insert(x.begin(), x.end());
    }
    return d;
  }

  // replace: det gamla innehållet ersätts; sure: skrivningen sker säkert.
  // Ett element (v[*]) skrivs alltid svagt: de andra elementen finns kvar.
  void write(State& s, const std::string& p, Deps d, bool replace, bool sure) const {
    if (p.find("[*]") != std::string::npos) {
      Deps old = read(s, p);
      d.insert(old.begin(), old.end());
      replace = false;
      sure = false;
    }
    if (replace) forEachWithin(s.vals, p, [&](auto it) { s.vals.erase(it); });
    s.vals[p] = std::move(d);
    s.written.insert(p);
    if (sure) s.always.insert(p);
  }

  static std::string stmtMarker(std::size_t b, std::size_t i) {
    return "\xc2\xa7" + std::to_string(b) + "." + std::to_string(i);
  }
  static std::string condMarker(std::size_t b) { return "\xc2\xa7" "c" + std::to_string(b); }

  // Satser som når en utdata: returvärdet, utdataparametrarna vid utgången,
  // anrop av kod med verkan utanför (sink_), villkoren för ett kast.
  void collectNoEffect(const std::vector<State>& out, const FlowSummary& s, const Deps& ret) {
    Deps effective = sink_;
    effective.insert(ret.begin(), ret.end());
    for (const auto& [o, d] : s.deps) effective.insert(d.begin(), d.end());
    for (unsigned b = 0; b < g_.blocks.size(); ++b) {
      if (!out[b].reached) continue;
      const FlowBlock& blk = g_.blocks[b];
      for (std::size_t i = 0; i < blk.stmts.size(); ++i)
        if (!blk.stmts[i].note.what.empty() && blk.stmts[i].kind != FlowStmt::Kind::ret &&
            !effective.contains(stmtMarker(b, i)))
          noEffect_.push_back(blk.stmts[i].note);
      if (!blk.condNote.what.empty() && blk.succs.size() > 1 && !effective.contains(condMarker(b)))
        noEffect_.push_back(blk.condNote);
    }
  }

  void transfer(State& s, const FlowBlock& b, const Deps& ctl0, Deps& ret) {
    if (!s.reached) return;
    const std::size_t bi = static_cast<std::size_t>(&b - g_.blocks.data());
    std::size_t index = 0;
    Deps ctl = ctl0;
    auto with = [&](Deps d) {
      d.insert(ctl.begin(), ctl.end());
      return d;
    };
    for (const FlowStmt& st : b.stmts) {
      ctl = ctl0;
      if (markers_) ctl.insert(stmtMarker(bi, index));
      ++index;
      switch (st.kind) {
        case FlowStmt::Kind::assign:
          write(s, st.target, with(resolve(s, st.values)), true, true);
          break;
        case FlowStmt::Kind::weak: {
          Deps d = with(resolve(s, st.values));
          Deps old = read(s, st.target);
          d.insert(old.begin(), old.end());
          write(s, st.target, std::move(d), false, false);
          break;
        }
        case FlowStmt::Kind::ret: {
          Deps d = with(resolve(s, st.values));
          ret.insert(d.begin(), d.end());
          break;
        }
        case FlowStmt::Kind::call:
          call(s, st.call, ctl);
          break;
      }
    }
  }

  void call(State& s, const FlowCall& c, const Deps& ctl) {
    const std::string result = "#" + std::to_string(c.id);
    auto it = c.usr.empty() ? callees_.end() : callees_.find(c.usr);
    const FlowSummary* callee = it != callees_.end() && it->second.known ? &it->second : nullptr;
    // Det den anropade läser genom argument i. Genom en skrivbar referens till
    // ett element (v[i]) beror läsningen också på indexet som valde elementet.
    auto argDeps = [&](std::size_t i, const std::string& sub) {
      if (i < c.refs.size() && !c.refs[i].empty()) {
        Deps d = read(s, c.refs[i] + sub);
        if (i < c.refIndex.size()) {
          Deps x = resolve(s, c.refIndex[i]);
          d.insert(x.begin(), x.end());
        }
        return d;
      }
      return i < c.args.size() ? resolve(s, c.args[i]) : Deps{};
    };
    // Kod med verkan utanför utdata (BRZ-7): det som når anropet har verkan.
    if (callee == nullptr || callee->params.size() != c.args.size() || callee->external) {
      sinkExternal_ = true;
      sink_.insert(ctl.begin(), ctl.end());
      for (std::size_t i = 0; i < c.args.size(); ++i) {
        Deps d = argDeps(i, "");
        sink_.insert(d.begin(), d.end());
      }
      for (const FlowValues& ix : c.refIndex) {
        Deps x = resolve(s, ix);
        sink_.insert(x.begin(), x.end());
      }
    }
    if (callee == nullptr || callee->params.size() != c.args.size()) {
      // Okänd kod: allt kan bero på allt, varje skrivbar referens kan skrivas.
      Deps all = ctl;
      for (std::size_t i = 0; i < c.args.size(); ++i) {
        Deps d = argDeps(i, "");
        all.insert(d.begin(), d.end());
      }
      for (const FlowValues& ix : c.refIndex) {
        Deps x = resolve(s, ix);
        all.insert(x.begin(), x.end());
      }
      write(s, result, all, true, true);
      for (const std::string& r : c.refs)
        if (!r.empty()) write(s, r, all, true, false);
      return;
    }
    // Sammanfattningen, översatt till anroparens argument. En utdata som
    // kanske skrivs bär redan sitt ingångsvärde (det gamla innehållet).
    auto mapped = [&](const Deps& inputs) {
      Deps d = ctl;
      for (const std::string& q : inputs) {
        std::string root = rootOf(q);
        auto pos = std::find(callee->params.begin(), callee->params.end(), root);
        if (pos == callee->params.end()) continue;
        Deps x = argDeps(static_cast<std::size_t>(pos - callee->params.begin()), q.substr(root.size()));
        d.insert(x.begin(), x.end());
      }
      return d;
    };
    std::vector<std::pair<std::string, Deps>> writes;  // läs allt före skrivningarna
    for (const auto& [o, inputs] : callee->deps) {
      if (o == "return") {
        writes.emplace_back(result, mapped(inputs));
        continue;
      }
      std::string root = rootOf(o);
      auto pos = std::find(callee->params.begin(), callee->params.end(), root);
      std::size_t i = static_cast<std::size_t>(pos - callee->params.begin());
      if (pos == callee->params.end() || i >= c.refs.size() || c.refs[i].empty()) continue;
      writes.emplace_back(c.refs[i] + o.substr(root.size()), mapped(inputs));
    }
    // Indexen som valde elementen (update(i, v[i])) gäller vid bindningen,
    // före anropet: räkna dem innan någon skrivning görs.
    std::vector<Deps> index(c.refIndex.size());
    for (std::size_t i = 0; i < c.refIndex.size(); ++i) index[i] = resolve(s, c.refIndex[i]);
    for (auto& [p, d] : writes) {
      bool sure = p == result;
      for (std::size_t i = 0; i < c.refs.size() && i < index.size(); ++i)
        if (!c.refs[i].empty() && within(p, c.refs[i])) d.insert(index[i].begin(), index[i].end());
      if (!sure)
        for (const std::string& a : callee->always) {
          std::string root = rootOf(a);
          auto pos = std::find(callee->params.begin(), callee->params.end(), root);
          std::size_t i = static_cast<std::size_t>(pos - callee->params.begin());
          if (pos != callee->params.end() && i < c.refs.size() && !c.refs[i].empty() &&
              within(p, c.refs[i] + a.substr(root.size())))
            sure = true;
        }
      write(s, p, std::move(d), true, sure);
    }
    if (!callee->deps.contains("return")) write(s, result, ctl, true, true);
  }

  State merge(const State& a, const State& b) const {
    if (!a.reached) return b;
    if (!b.reached) return a;
    State m;
    m.reached = true;
    std::set<std::string> keys;
    for (const auto& kv : a.vals) keys.insert(kv.first);
    for (const auto& kv : b.vals) keys.insert(kv.first);
    for (const std::string& k : keys) {
      Deps d = read(a, k), e = read(b, k);
      d.insert(e.begin(), e.end());
      m.vals[k] = std::move(d);
    }
    std::set_union(a.written.begin(), a.written.end(), b.written.begin(), b.written.end(),
                   std::inserter(m.written, m.written.end()));
    for (const std::string& p : a.always)
      if (std::ranges::any_of(b.always, [&](const std::string& q) { return within(p, q); })) m.always.insert(p);
    for (const std::string& p : b.always)
      if (std::ranges::any_of(a.always, [&](const std::string& q) { return within(p, q); })) m.always.insert(p);
    return m;
  }

  // Postdominatorer och styrberoenden (b beror på a när en gren från a leder
  // till b men b inte alltid följer efter a).
  void postDominators() {
    const std::size_t n = g_.blocks.size();
    std::vector<std::set<unsigned>> pdom(n);
    std::set<unsigned> all;
    for (unsigned b = 0; b < n; ++b) all.insert(b);
    for (unsigned b = 0; b < n; ++b) pdom[b] = b == g_.exit ? std::set<unsigned>{b} : all;
    for (bool changed = true; changed;) {
      changed = false;
      for (unsigned b = 0; b < n; ++b) {
        if (b == g_.exit) continue;
        std::set<unsigned> p;
        bool first = true;
        for (unsigned s : g_.blocks[b].succs) {
          if (first) p = pdom[s];
          else {
            std::set<unsigned> q;
            std::set_intersection(p.begin(), p.end(), pdom[s].begin(), pdom[s].end(), std::inserter(q, q.end()));
            p = std::move(q);
          }
          first = false;
        }
        if (first) p.clear();  // inga efterföljare (når inte utgången)
        p.insert(b);
        if (p != pdom[b]) {
          pdom[b] = std::move(p);
          changed = true;
        }
      }
    }
    cd_.assign(n, {});
    for (unsigned a = 0; a < n; ++a) {
      if (g_.blocks[a].succs.size() < 2) continue;
      for (unsigned s : g_.blocks[a].succs)
        for (unsigned b = 0; b < n; ++b)
          if (pdom[s].contains(b) && !(pdom[a].contains(b) && b != a)) cd_[b].insert(a);
    }
  }

  FlowSummary summary(const State& exit, const Deps& ret) const {
    FlowSummary s;
    s.known = true;
    s.params = g_.params;
    if (g_.returnsValue) s.deps["return"] = ret;
    if (!exit.reached) return s;
    for (std::size_t i = 0; i < g_.params.size(); ++i) {
      if (i >= g_.outputs.size() || !g_.outputs[i]) continue;
      for (const std::string& p : exit.written) {
        if (rootOf(p) != g_.params[i]) continue;
        // Inuti en plats som alltid skrivs: den platsens rad säger allt.
        if (std::ranges::any_of(exit.written, [&](const std::string& a) {
              return a != p && within(p, a) && exit.always.contains(a);
            }))
          continue;
        s.deps[p] = read(exit, p);
        if (std::ranges::any_of(exit.always, [&](const std::string& a) { return within(p, a); }))
          s.always.insert(p);
      }
    }
    return s;
  }

  const FlowGraph& g_;
  const std::map<std::string, FlowSummary>& callees_;
  std::set<std::string> inputs_;
  std::vector<std::set<unsigned>> cd_;
  bool markers_ = false;
  Deps sink_;                 // når kod med verkan utanför (anrop, kast)
  bool sinkExternal_ = false;  // ett sådant anrop finns
  std::vector<FlowNote> noEffect_;
};

}  // namespace

FlowSummary summarize(const FlowGraph& g, const std::map<std::string, FlowSummary>& callees) {
  if (!g.unsupported.empty()) {
    FlowSummary s;
    s.why = g.unsupported;
    return s;
  }
  return Analysis(g, callees).run();
}

std::map<std::string, FlowSummary> computeFlows(const std::map<std::string, FlowGraph>& graphs) {
  // Från botten: varje utdata skrivs säkert och beror på inget. Beroendena
  // kan bara växa och de säkra skrivningarna bara krympa, så fixpunkten nås.
  std::map<std::string, FlowSummary> s;
  for (const auto& [usr, g] : graphs) {
    FlowSummary b;
    if (!g.unsupported.empty()) {
      b.why = g.unsupported;
    } else {
      b.known = true;
      b.params = g.params;
      if (g.returnsValue) b.deps["return"] = {};
      for (std::size_t i = 0; i < g.params.size(); ++i)
        if (i < g.outputs.size() && g.outputs[i]) {
          b.deps[g.params[i]] = {};
          b.always.insert(g.params[i]);
        }
    }
    s[usr] = std::move(b);
  }
  for (int round = 0; round < 100; ++round) {
    bool changed = false;
    for (const auto& [usr, g] : graphs) {
      if (!g.unsupported.empty()) continue;
      FlowSummary next = summarize(g, s);
      if (!(next == s[usr])) {
        s[usr] = std::move(next);
        changed = true;
      }
    }
    if (!changed) return s;
  }
  for (auto& [usr, f] : s)
    if (f.known) {
      f = FlowSummary{};
      f.why = "flow did not converge";
    }
  return s;
}

std::string describe(const FlowNote& n) { return n.what + " at line " + std::to_string(n.line); }

void findNoEffect(const std::map<std::string, FlowGraph>& graphs, std::map<std::string, FlowSummary>& flows) {
  for (const auto& [usr, g] : graphs) {
    auto it = flows.find(usr);
    if (it == flows.end() || !it->second.known || !g.unsupported.empty()) continue;
    Analysis a(g, flows, true);
    a.run();
    it->second.noEffect = a.noEffect();
  }
}

namespace {
// "d.value@Derived" -> "d.value"; qualified: "d.Derived::value".
std::string shown(const std::string& p, bool qualified) {
  std::string out;
  for (std::size_t i = 0; i < p.size();) {
    std::size_t at = p.find('@', i);
    if (at == std::string::npos) {
      out += p.substr(i);
      break;
    }
    std::size_t end = std::min(p.find('.', at), p.find('[', at));
    if (end == std::string::npos) end = p.size();
    std::size_t start = p.rfind('.', at);
    start = start == std::string::npos || start < i ? i : start + 1;
    out += p.substr(i, start - i);
    std::string name = p.substr(start, at - start), cls = p.substr(at + 1, end - at - 1);
    out += qualified ? cls + "::" + name : name;
    i = end;
  }
  return out;
}
}  // namespace

std::string checkRequirement(const FlowSummary& s, const FlowRequirement& r) {
  if (!r.error.empty()) return r.error;
  if (!s.known) return "flow not computed: " + s.why;
  auto inside = [](const std::string& p, const std::string& q) {
    return p == q || (p.size() > q.size() && p.starts_with(q) && (p[q.size()] == '.' || p[q.size()] == '['));
  };
  // Det utdatan och dess delar beror på. En del som ingen skrivning täcker
  // behåller sitt ingångsvärde.
  std::set<std::string> deps;
  bool whole = false;  // en skrivning som alltid sker täcker hela utdatan
  for (const auto& [o, d] : s.deps)
    if (inside(o, r.output) || inside(r.output, o)) {
      deps.insert(d.begin(), d.end());
      if (inside(r.output, o) && s.always.contains(o)) whole = true;
    }
  if (!whole && r.output != "return") deps.insert(r.output);
  const std::string who = r.output == "return" ? "the result" : shown(r.output, false);
  std::vector<std::string> bad;
  for (const std::string& d : deps)
    if (std::ranges::none_of(r.allowed, [&](const std::string& a) { return inside(d, a); })) bad.push_back(d);
  if (bad.empty()) return "";
  std::string out = who + " depends on " + shown(bad.front(), false) + ", which PORK_DEPENDS does not allow";
  if (bad.size() > 1) out += " (+" + std::to_string(bad.size() - 1) + " more)";
  return out;
}

std::vector<std::string> flowLines(const FlowSummary& s) {
  std::vector<std::string> out;
  // Två olika platser som skulle se likadana ut (skuggade fält): med klass.
  std::map<std::string, std::set<std::string>> byText;
  for (const auto& [o, d] : s.deps) {
    byText[shown(o, false)].insert(o);
    for (const std::string& x : d) byText[shown(x, false)].insert(x);
  }
  auto show = [&](const std::string& p) { return shown(p, byText[shown(p, false)].size() > 1); };
  auto line = [&](const std::string& o0, const std::set<std::string>& d0) {
    std::string o = o0 == "return" ? o0 : show(o0);
    std::set<std::string> d;
    for (const std::string& x : d0) d.insert(show(x));
    std::string t = o + " <- ";
    if (d.empty()) t += "nothing";
    const char* sep = "";
    for (const std::string& x : d) {
      t += sep + x;
      sep = ", ";
    }
    return t;
  };
  if (auto it = s.deps.find("return"); it != s.deps.end()) out.push_back(line("return", it->second));
  for (const auto& [o, d] : s.deps) {
    if (o == "return") continue;
    out.push_back(line(o, d) + (s.always.contains(o) ? " (always written)" : " (maybe written)"));
  }
  for (const FlowNote& n : s.noEffect) out.push_back("no effect: " + describe(n));
  return out;
}

}  // namespace pork
