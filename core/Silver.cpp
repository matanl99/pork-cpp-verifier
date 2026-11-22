#include "Silver.hpp"

#include <algorithm>
#include <atomic>
#include <optional>
#include <set>
#include <thread>
#include <vector>

namespace pork {

namespace {

std::string basename(const std::string& path) {
  auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// En anropad funktion med PORK_POST används modulärt: bara dess kontrakt.
bool modular(const FunctionFacts& f) { return f.c && *f.c && !(*f.c)->contract.empty(); }

// Funktionen och allt den anropar som har en översättning att ta med, i
// besöksordning (roten först). Betrodda och odefinierade anrop har ingen:
// de går till kontrakt i contracts/. Funktioner med PORK_POST tas med som
// kontraktsversion, och det de i sin tur anropar följs inte.
// Metod (usr) -> gränssnittskontraktet i kontraktsfilen.
using ContractMap = std::map<std::string, const FunctionFacts*>;

ContractMap contractsOf(const FactsMap& facts, const std::map<std::string, Verdict>& verdicts) {
  ContractMap out;
  for (const auto& [usr, f] : facts)
    if (!f.contractFor.empty() && verdicts.at(usr).kind != Verdict::Kind::notStone) out.emplace(f.contractFor, &f);
  return out;
}

// Metodens namn för ett gränssnittskontrakt ("IClock::now").
std::string contractedName(const FunctionFacts& contract) {
  if (contract.c && *contract.c)
    if (auto it = (*contract.c)->contracts.find((*contract.c)->name); it != (*contract.c)->contracts.end())
      return it->second;
  return contract.name;
}

// Kända implementationer av en virtuell metod: den själv (om definierad) och overrides.
std::vector<const FunctionFacts*> implementations(const FactsMap& facts, const std::string& method) {
  std::vector<const FunctionFacts*> out;
  for (const auto& [usr, f] : facts)
    if (f.contractFor.empty() && (usr == method || f.overrides.contains(method))) out.push_back(&f);
  return out;
}

std::vector<const FunctionFacts*> closure(const FactsMap& facts, const ContractMap& contracts,
                                          const FunctionFacts& root) {
  std::vector<const FunctionFacts*> order{&root};
  std::set<std::string> seen{root.usr};
  auto visit = [&](const std::string& usr) {
    auto it = facts.find(usr);
    if (it == facts.end() || it->second.trusted || !seen.insert(usr).second) return;
    order.push_back(&it->second);
  };
  for (std::size_t i = 0; i < order.size(); ++i) {
    if (i > 0 && modular(*order[i])) {  // bara det kontraktet (PRE/POST/invarianter) anropar
      for (const std::string& usr : (*order[i]->c)->contractCallees) visit(usr);
      continue;
    }
    for (const auto& [usr, callee] : order[i]->callees) {
      if (usr.starts_with(kDispatchPrefix)) {  // gränssnittskontraktet, aldrig en implementation
        if (auto it = contracts.find(usr.substr(kDispatchPrefix.size())); it != contracts.end()) visit(it->second->usr);
        continue;
      }
      visit(usr);
    }
    if (order[i]->c && *order[i]->c)  // typinvarianter som villkoren anropar (oöversatt: fel nedan)
      for (const std::string& usr : (*order[i]->c)->contractCallees) visit(usr);
  }
  return order;
}

std::string reasonFor(const std::vector<ProofFailure>& failures,
                      const std::map<std::string, std::string>& contracts) {
  const ProofFailure& f = failures.front();
  std::string r;
  if (!f.missingFunction.empty()) {
    auto it = contracts.find(f.missingFunction);
    r = "no contract for '" + (it != contracts.end() ? it->second : f.missingFunction) + "'";
  } else {
    r = f.description;
    if (!f.file.empty()) r += " at " + basename(f.file) + ":" + std::to_string(f.line);
  }
  if (failures.size() > 1) r += " (+" + std::to_string(failures.size() - 1) + " more)";
  return r;
}

// Kör prove() för alla program, upp till `jobs` åt gången.
std::vector<ProofResult> proveAll(
    const std::vector<std::string>& programs, IProver& prover, unsigned jobs) {
  std::vector<ProofResult> results(programs.size());
  std::atomic<std::size_t> next{0};
  auto worker = [&] {
    for (std::size_t i = next++; i < programs.size(); i = next++) results[i] = prover.prove(programs[i]);
  };
  std::size_t n = std::min<std::size_t>(std::max(jobs, 1u), programs.size());
  {
    // Egen scope: trådarna är inväntade innan results flyttas ut. Vid return
    // initieras returvärdet (en flytt, om den inte elideras) innan lokala
    // variabler förstörs, så jthread i samma scope väntade in för sent.
    std::vector<std::jthread> threads;
    for (std::size_t t = 1; t < n; ++t) threads.emplace_back(worker);
    worker();
  }
  return results;
}

}  // namespace

void silver(const FactsMap& facts, std::map<std::string, Verdict>& verdicts, IProver& prover,
            unsigned jobs, const std::function<bool(const FunctionFacts&)>& wanted) {
  struct Lsp {
    std::size_t proof;
    std::string method;  // gränssnittsmetoden vars kontrakt bevisas
  };
  struct Job {
    Verdict* verdict;
    std::vector<const CFunction*> parts;
    std::map<std::string, std::string> contracts;
    std::vector<const FunctionFacts*> relies;  // vars eftervillkor beviset bygger på
    std::size_t proof;                       // index i programs
    std::optional<std::size_t> selfTest;     // självtestet av förvillkoren, om några
    std::vector<Lsp> lsp;                    // Liskov-bevisen (implementationen mot kontrakten)
    // Implementationer bakom dispatchande anrop: kontraktet gäller bara om de
    // är bevisade (sin kod och kontraktet). Par (implementation, metod).
    std::vector<std::pair<const FunctionFacts*, std::string>> implementations;
  };
  std::vector<Job> work;
  std::vector<std::string> programs;
  const ContractMap interfaceContracts = contractsOf(facts, verdicts);

  // Urvalet (`wanted`, --only) anger vilka bevis som startas. De eftervillkor
  // och implementationer ett bevis bygger på läggs till i listan när beviset
  // byggs, så att en vald funktion får samma dom som i hela programmet.
  auto candidate = [&](const FunctionFacts& f) {
    return verdicts.at(f.usr).kind == Verdict::Kind::stone && f.contractFor.empty();  // kontraktet själv bevisas inte
  };
  std::vector<const FunctionFacts*> todo;
  std::set<const FunctionFacts*> queued;
  auto enqueue = [&](const FunctionFacts* g) {
    if (candidate(*g) && queued.insert(g).second) todo.push_back(g);
  };
  for (const auto& [usr, f] : facts) {
    // Implementationer av kontrakterade metoder bevisas alltid: anropare bygger på dem.
    bool implements = false;
    for (const auto& [m, c] : interfaceContracts) implements = implements || m == usr || f.overrides.contains(m);
    if (!wanted || wanted(f) || implements) enqueue(&f);
  }

  for (std::size_t k = 0; k < todo.size(); ++k) {
    const FunctionFacts& f = *todo[k];
    Verdict& v = verdicts.at(f.usr);

    std::vector<const CFunction*> parts;
    std::vector<const FunctionFacts*> relies;
    std::vector<std::pair<const FunctionFacts*, std::string>> impls;
    std::set<const CFunction*> asContract;
    for (const FunctionFacts* g : closure(facts, interfaceContracts, f)) {
      std::string who = g == &f ? "" : " '" + g->name + "'";
      if (!g->c) {
        v.reason = "not translated to C" + who;
        break;
      }
      if (!*g->c) {
        v.reason = "cannot translate" + who + " to C: " + g->c->error();
        break;
      }
      parts.push_back(&**g->c);
      if (!g->contractFor.empty()) {
        asContract.insert(&**g->c);
        continue;
      }
      if (g != &f && modular(*g)) {
        relies.push_back(g);
        asContract.insert(&**g->c);
        continue;
      }
      for (const auto& [calleeUsr, callee] : g->callees)
        if (calleeUsr.starts_with(kDispatchPrefix)) {
          std::string m = calleeUsr.substr(kDispatchPrefix.size());
          if (!interfaceContracts.contains(m)) continue;
          for (const FunctionFacts* i : implementations(facts, m))
            if (i != &f) impls.emplace_back(i, contractedName(*interfaceContracts.at(m)));
        }
    }
    if (!v.reason.empty()) continue;
    for (const FunctionFacts* g : relies) enqueue(g);
    for (const auto& [g, method] : impls) enqueue(g);

    // Typer och kontraktsprototyper (en gång var, beroendeordningen bevaras),
    // sedan alla prototyper, definitioner och sist rotens ingång.
    std::string program;
    std::set<std::string> declared;
    std::map<std::string, std::string> contracts;
    for (const CFunction* c : parts) {
      for (const auto& [key, text] : c->decls)
        if (declared.insert(key).second) program += text;
      contracts.insert(c->contracts.begin(), c->contracts.end());
    }
    // Ärvda förvillkor: kontraktets __pre (med dess typer och villkor), annars 1.
    for (const CFunction* c : parts)
      for (const auto& [macro, method] : c->inherits) {
        if (!declared.insert("#define " + macro).second) continue;
        auto it = interfaceContracts.find(method);
        if (it == interfaceContracts.end() || !it->second->c || !*it->second->c) {
          program += "#define " + macro + "(...) 1\n";
          continue;
        }
        const CFunction& ic = **it->second->c;
        for (const auto& [key, text] : ic.decls)
          if (declared.insert(key).second) program += text;
        if (declared.insert("predicates:" + ic.name).second) program += ic.predicates;
        program += "#define " + macro + " " + ic.name + "__pre\n";
      }
    // Externa anrop utan översättning i programmet: till kontraktet.
    std::set<std::string> defined;
    for (const CFunction* c : parts) defined.insert(c->name);
    for (const CFunction* c : parts)
      for (const auto& [name, contract] : c->externals)
        if (!contract.empty() && !defined.contains(name) && declared.insert("#define " + name).second)
          program += "#define " + name + " " + contract + "\n";
    for (const CFunction* c : parts) program += c->prototype;
    for (const CFunction* c : parts) program += asContract.contains(c) ? c->contract : c->definition;

    // Liskov: samma program, plus kontraktets villkor och en egen ingång.
    std::vector<Lsp> lsp;
    for (const auto& [method, entry] : parts.front()->lsp) {
      auto it = interfaceContracts.find(method);
      if (it == interfaceContracts.end() || !it->second->c || !*it->second->c) continue;
      const CFunction& ic = **it->second->c;
      std::string p = program;
      std::set<std::string> more = declared;
      for (const auto& [key, text] : ic.decls)
        if (more.insert(key).second) p += text;
      if (more.insert("predicates:" + ic.name).second) p += ic.predicates;
      lsp.push_back({programs.size(), contractedName(*it->second)});
      programs.push_back(p + entry);
    }

    std::optional<std::size_t> selfTest;
    bool anyInherited = false;
    for (const auto& [macro, method] : parts.front()->inherits) anyInherited = anyInherited || interfaceContracts.contains(method);
    if (!parts.front()->satisfiable.empty() && (!parts.front()->inheritsOnly || anyInherited)) {
      selfTest = programs.size();
      programs.push_back(program + parts.front()->satisfiable);
    }
    program += parts.front()->entry;
    work.push_back({&v, std::move(parts), std::move(contracts), std::move(relies), programs.size(), selfTest,
                    std::move(lsp), std::move(impls)});
    programs.push_back(std::move(program));
  }

  auto results = proveAll(programs, prover, jobs);
  for (std::size_t i = 0; i < work.size(); ++i) {
    Verdict& v = *work[i].verdict;
    if (work[i].selfTest) {
      // Självtestet ska misslyckas; gör det inte kan förvillkoren aldrig gälla.
      const ProofResult& sat = results[*work[i].selfTest];
      if (!sat) {
        v.reason = "prover failed (precondition self-test): " + sat.error().message;
        continue;
      }
      if (std::ranges::none_of(*sat, [](const ProofFailure& f) { return f.description == kSatisfiableMarker; })) {
        v.reason = "preconditions can never hold (PORK_PRE, leading asserts and invariants contradict each other)";
        continue;
      }
    }
    const ProofResult& failures = results[work[i].proof];
    if (!failures)
      v.reason = "prover failed: " + failures.error().message;
    else if (failures->empty()) {
      v.kind = Verdict::Kind::silver;
      for (const CFunction* c : work[i].parts) v.assumptions.insert(c->assumptions.begin(), c->assumptions.end());
    }
    else {
      v.reason = reasonFor(*failures, work[i].contracts);
      v.modelInputs = failures->front().modelInputs;
    }
    if (v.kind != Verdict::Kind::silver) continue;
    for (const Lsp& l : work[i].lsp) {
      const ProofResult& r = results[l.proof];
      std::string why;
      if (!r)
        why = "prover failed: " + r.error().message;
      else if (!r->empty())
        why = reasonFor(*r, work[i].contracts);
      if (why.empty()) continue;
      v.kind = Verdict::Kind::stone;
      v.reason = "does not satisfy the contract of '" + l.method + "': " + why;
      break;
    }
  }

  // BRZ-4: ett brutet flödeskrav (Analyse avgör Bronze före Silver) fäller
  // Silver, före fixpunkten så att anropare som bygger på eftervillkoret
  // också faller. Beviset gäller fortfarande körtidsfelen (oraklet).
  for (const Job& job : work) {
    Verdict& v = *job.verdict;
    if (v.kind != Verdict::Kind::silver || !v.bronze || v.bronze->empty()) continue;
    v.kind = Verdict::Kind::stone;
    v.reason.clear();
    v.runtimeProved = true;
  }

  // Ett bevis som bygger på ett eftervillkor gäller bara om det eftervillkoret
  // själv är bevisat. Upprepa tills inget ändras (kedjor av anrop).
  for (bool changed = true; changed;) {
    changed = false;
    for (const Job& job : work) {
      Verdict& v = *job.verdict;
      if (v.kind != Verdict::Kind::silver) continue;
      for (const FunctionFacts* g : job.relies) {
        if (verdicts.at(g->usr).kind == Verdict::Kind::silver) continue;
        v.kind = Verdict::Kind::stone;
        v.reason = verdicts.at(g->usr).runtimeProved
                       ? "relies on the postcondition of '" + g->name + "', which is not bronze"
                       : "relies on the unproven postcondition of '" + g->name + "'";
        changed = true;
        break;
      }
      if (v.kind != Verdict::Kind::silver) continue;
      for (const auto& [g, method] : job.implementations) {
        if (verdicts.at(g->usr).kind == Verdict::Kind::silver) continue;
        v.kind = Verdict::Kind::stone;
        v.reason = "relies on the contract of '" + method + "', and the implementation '" + g->name + "' is not silver";
        changed = true;
        break;
      }
    }
  }

  // SLV-43: beviset visar att inget kast nås (också i anropade funktioner,
  // som antingen ingår i programmet eller själva är Silver): antagandet att
  // funktionen kan kasta gäller inte längre.
  for (const Job& job : work)
    if (job.verdict->kind == Verdict::Kind::silver)
      std::erase_if(job.verdict->assumptions, [](const std::string& a) { return a.starts_with("throws "); });
}

namespace {

const std::string kOracleMarker = "pork flow oracle";

// p ligger inom q: samma plats eller en del av den ("t.total@T" inom "t").
bool inside(const std::string& p, const std::string& q) {
  return p == q || (p.size() > q.size() && p.starts_with(q) && (p[q.size()] == '.' || p[q.size()] == '['));
}

// "t.total@Totals" -> "t.total" (för utskrift).
std::string plain(const std::string& p) {
  std::string out;
  bool skip = false;
  for (char c : p) {
    if (c == '@') skip = true;
    else if (c == '.' || c == '[') skip = false;
    if (!skip) out += c;
  }
  return out;
}

}  // namespace

void flowOracle(const FactsMap& facts, std::map<std::string, Verdict>& verdicts,
                const std::map<std::string, FlowSummary>& flows, IProver& prover, unsigned jobs, bool selfTest) {
  struct Probe {
    Verdict* verdict;
    std::string leaf;
    std::set<std::string> deps;
    std::size_t program;
  };
  std::vector<Probe> probes;
  std::vector<std::string> programs;
  const ContractMap none;
  for (const auto& [usr, s] : flows) {
    const FunctionFacts& f = facts.at(usr);
    Verdict& v = verdicts.at(usr);
    if (!s.known) continue;
    if (v.kind != Verdict::Kind::silver && !v.runtimeProved) {
      v.flowOracle.push_back("untested: not silver");
      continue;
    }
    const CFunction& root = **f.c;
    if (!root.oracle) {
      v.flowOracle.push_back("untested: " + root.oracleWhy);
      continue;
    }
    // Programmet: funktionen och allt den anropar, med kod. Ett kontrakt ger
    // godtyckliga värden (två körningar kan skilja sig utan att indata gör
    // det), så funktioner som når ett kontrakt prövas inte.
    std::vector<const CFunction*> parts;
    std::string why;
    for (const FunctionFacts* g : closure(facts, none, f)) {
      if (!g->c || !*g->c) {
        why = "'" + g->name + "' is not translated";
        break;
      }
      const CFunction& c = **g->c;
      if (g != &f && modular(*g)) why = "calls '" + g->name + "', which has a postcondition contract";
      else if (!c.inherits.empty()) why = "inherited precondition";
      for (const auto& [callee, unused] : g->callees)
        if (callee.starts_with(kDispatchPrefix)) why = "dispatching call";
      if (!why.empty()) break;
      parts.push_back(&c);
    }
    if (why.empty()) {
      // Ett anrop till en annan TU använder översättningen om den finns med
      // (som i silver()); då är det inget kontrakt. Nycklarna är C-namnen.
      std::set<std::string> defined;
      for (const CFunction* c : parts) defined.insert(c->name);
      std::set<std::string> replaced;  // kontrakt vars funktion finns översatt
      for (const CFunction* c : parts)
        for (const auto& [name, contract] : c->externals) {
          if (!defined.contains(name)) why = "calls a function without translation";
          if (!contract.empty()) replaced.insert(contract);
        }
      for (const CFunction* c : parts)
        for (const auto& [name, cxx] : c->contracts)
          if (why.empty() && !defined.contains(name) && !replaced.contains(name))
            why = "calls the contract of '" + cxx + "'";
    }
    if (!why.empty()) {
      v.flowOracle.push_back("untested: " + why);
      continue;
    }
    std::string program;
    std::set<std::string> declared;
    for (const CFunction* c : parts)
      for (const auto& [key, text] : c->decls)
        if (declared.insert(key).second) program += text;
    for (const CFunction* c : parts) program += c->prototype;
    for (const CFunction* c : parts) program += c->definition;
    program += root.entry.substr(0, root.entry.find("void pork_harness(void)"));

    const FlowOracleHarness& h = *root.oracle;
    std::size_t before = probes.size();
    for (const OracleLeaf& out : h.leaves) {
      if (!out.output) continue;
      // Vad delen kan bero på: utdatan som täcker den (eller ingen: oförändrad).
      std::set<std::string> deps;
      bool covered = false;
      for (const auto& [o, d] : s.deps)
        if (inside(out.path, o)) {
          covered = true;
          deps.insert(d.begin(), d.end());
        }
      if (!covered) deps.insert(out.path);
      if (selfTest) {  // tappa ett beroende (inte det egna ingångsvärdet), med sina delar
        for (const std::string& d : deps)
          if (!inside(out.path, d) && !inside(d, out.path)) {
            std::string drop = d;
            std::erase_if(deps, [&](const std::string& x) { return inside(x, drop) || inside(drop, x); });
            break;
          }
      }
      std::string text = "void pork_harness(void)\n{\n" + h.setup;
      for (const OracleLeaf& in : h.leaves) {
        if (in.path == "return" || in.path.starts_with("return.") || in.path.starts_with("return[")) continue;
        bool fixed = std::ranges::any_of(deps, [&](const std::string& d) { return inside(in.path, d) || inside(d, in.path); });
        if (fixed) text += "  __CPROVER_assume(" + in.assume + ");\n";
      }
      text += h.run + "  __CPROVER_assert(" + out.compare + ", \"" + kOracleMarker + "\");\n}\n";
      probes.push_back({&v, out.path, std::move(deps), programs.size()});
      programs.push_back(program + text);
    }
    if (probes.size() == before) v.flowOracle.push_back("untested: no outputs");
  }

  auto results = proveAll(programs, prover, jobs);
  std::map<Verdict*, std::pair<unsigned, unsigned>> counts;  // (prövade, hål)
  for (const Probe& p : probes) {
    const ProofResult& r = results[p.program];
    auto& [tested, holes] = counts[p.verdict];
    if (!r) {
      p.verdict->flowOracle.push_back("untested " + plain(p.leaf) + ": prover failed: " + r.error().message);
      continue;
    }
    ++tested;
    if (std::ranges::any_of(*r, [](const ProofFailure& f) { return f.description == kOracleMarker; })) {
      ++holes;
      std::string d;
      for (const std::string& x : p.deps) d += (d.empty() ? "" : ", ") + plain(x);
      p.verdict->flowOracle.push_back("HOLE " + plain(p.leaf) + " depends on more than {" + d + "}");
    }
  }
  for (auto& [v, c] : counts)
    if (c.second == 0 && c.first > 0) v->flowOracle.push_back("ok (" + std::to_string(c.first) + " parts)");
}

}  // namespace pork
