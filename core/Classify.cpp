#include "Classify.hpp"

namespace pork {

namespace {

using Kind = Verdict::Kind;

bool isTrustedWithoutDefinition(const Callee& c, const TrustList& trust) {
  return c.trusted || trust.trustsFunction(c.qualifiedName) || (!c.alias.empty() && trust.trustsFunction(c.alias));
}

// Kända implementationer av en virtuell metod (usr): metoden själv, om den
// har en definition, och alla overrides i de analyserade källorna.
using Implementations = std::map<std::string, std::vector<std::string>>;

Implementations implementationsOf(const FactsMap& facts) {
  Implementations impl;
  for (const auto& [usr, f] : facts) {
    impl[usr].push_back(usr);
    for (const std::string& base : f.overrides) impl[base].push_back(usr);
  }
  return impl;
}

const std::vector<std::string>& dispatchTargets(const Implementations& impl, const std::string& calleeUsr) {
  static const std::vector<std::string> none;
  if (!calleeUsr.starts_with(kDispatchPrefix)) return none;
  auto it = impl.find(calleeUsr.substr(kDispatchPrefix.size()));
  return it == impl.end() ? none : it->second;
}

// STN-55: ett referensargument i ett dispatchande anrop får inte kunna vara
// samma objekt som något implementationen når: en kontextreferens som når
// argumentets typ (eller tvärtom), eller ett eget fält, när någon annan kan ha
// en referens dit (en kontextreferens någonstans i programmet till en typ som
// fältet innehåller) eller implementationen kan anropa tillbaka (portfält).
Reason dispatchAlias(const FactsMap& facts, const Implementations& impl, const FunctionFacts& f) {
  std::vector<const RefShape*> programRefs;
  for (const auto& [usr, g] : facts)
    for (const RefShape& r : g.contextRefs) programRefs.push_back(&r);
  for (const DispatchCall& d : f.dispatchCalls) {
    auto it = impl.find(d.usr);
    if (it == impl.end()) continue;
    for (const std::string& target : it->second) {
      const FunctionFacts& g = facts.at(target);
      for (const ArgShape& a : d.args) {
        for (const RefShape& r : g.contextRefs) {
          // Argumentet i referensens objekt (känd väg: en av typerna längs
          // vägen), eller referensens objekt i argumentet.
          bool inside = a.known ? a.containers.contains(r.type) : r.reach.contains(a.type);
          bool overlap = inside || a.reach.contains(r.type);
          if (overlap && !(a.isConst && r.isConst))
            return "argument '" + a.text + "' may refer to the same object as reference field '" + r.field +
                   "' of the implementation '" + g.name + "'";
        }
        if (!g.fieldReach.contains(a.type)) continue;
        bool reachable = g.hasPorts;
        for (const RefShape* r : programRefs)
          reachable = reachable || (r->reach.contains(a.type) && g.fieldReach.contains(r->type));
        if (reachable && !(a.isConst && g.constMethod && !g.hasPorts))
          return "argument '" + a.text + "' may refer to a field of the implementation '" + g.name + "'" +
                 (g.hasPorts ? " (it has ports and may call back)" : " (a reference field elsewhere may refer to it)");
      }
    }
  }
  return std::nullopt;
}

// Steg 1: degradera Stone-funktioner som anropar något som inte håller. Ett
// dispatchande anrop håller bara om varje känd implementation håller; okända
// (utanför källorna) är ett antagande.
void demote(const FactsMap& facts, const TrustList& trust, const Implementations& impl,
            std::map<std::string, Verdict>& v) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& [usr, f] : facts) {
      if (v.at(usr).kind != Kind::stone) continue;
      for (const auto& [calleeUsr, callee] : f.callees) {
        Reason why;
        for (const std::string& target : dispatchTargets(impl, calleeUsr))
          if (v.at(target).kind == Kind::notStone) {
            why = "dispatches to non-stone '" + facts.at(target).name + "'";
            break;
          }
        auto it = v.find(calleeUsr);
        if (!why && it == v.end() && !isTrustedWithoutDefinition(callee, trust))
          why = "calls '" + callee.name + "' with no definition in the analysed sources" +
                (callee.qualifiedName.empty() ? std::string()
                                              : " (trust it with '" + callee.qualifiedName + "' in a trust file)");
        else if (!why && it != v.end() && it->second.kind == Kind::notStone)
          why = "calls non-stone '" + callee.name + "'";
        if (why) {
          v[usr] = Verdict::notStone(*why);
          changed = true;
          break;
        }
      }
    }
  }
}

// Steg 2: samla antaganden. Mängderna växer bara, så fixpunkten nås. Ett
// dispatchande anrop ger "dispatch I::f" och de kända implementationernas antaganden.
void collectAssumptions(const FactsMap& facts, const Implementations& impl, std::map<std::string, Verdict>& v) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& [usr, f] : facts) {
      if (v.at(usr).kind != Kind::stone) continue;
      std::set<std::string> found = f.assumedTypes;
      for (const auto& [calleeUsr, callee] : f.callees) {
        for (const std::string& target : dispatchTargets(impl, calleeUsr)) {
          const Verdict& t = v.at(target);
          if (t.kind == Kind::trusted) found.insert(facts.at(target).name);
          else found.insert(t.assumptions.begin(), t.assumptions.end());
        }
        auto it = v.find(calleeUsr);
        if (it == v.end())
          found.insert(callee.qualifiedName);  // betrodd utan definition: namnet som betros
        else if (it->second.kind == Kind::trusted)
          found.insert(callee.name);  // betrodd definition (pork::trusted)
        else
          found.insert(it->second.assumptions.begin(), it->second.assumptions.end());
      }
      auto& mine = v.at(usr).assumptions;
      std::size_t before = mine.size();
      mine.insert(found.begin(), found.end());
      changed = changed || mine.size() != before;
    }
  }
}

}  // namespace

std::map<std::string, Verdict> classify(const FactsMap& facts, const TrustList& trust) {
  std::map<std::string, Verdict> v;
  for (const auto& [usr, f] : facts) {
    if (f.trusted)
      v[usr] = Verdict::trust();
    else if (f.local)
      v[usr] = Verdict::notStone(*f.local);
    else
      v[usr] = Verdict::stone();
  }
  // Gränssnittskontrakt: ett per metod, och inte både i deklarationen och i
  // kontraktsfilen (C++26: pre/post på deklarationen räknas som deklarationens).
  std::map<std::string, std::string> contractOf;
  for (const auto& [usr, f] : facts) {
    if (f.contractFor.empty() || v.at(usr).kind == Kind::notStone) continue;
    auto m = facts.find(f.contractFor);
    if (m != facts.end() && m->second.declContract)
      v[usr] = Verdict::notStone("'" + m->second.name + "' has a contract in its definition and in the contract file");
    else if (!contractOf.emplace(f.contractFor, usr).second)
      v[usr] = Verdict::notStone("a second interface contract for the same method ('" + facts.at(contractOf.at(f.contractFor)).name + "')");
  }
  Implementations impl = implementationsOf(facts);
  for (const auto& [usr, f] : facts)
    if (v.at(usr).kind == Kind::stone)
      if (Reason r = dispatchAlias(facts, impl, f)) v[usr] = Verdict::notStone(*r);
  demote(facts, trust, impl, v);
  collectAssumptions(facts, impl, v);
  return v;
}

}  // namespace pork
