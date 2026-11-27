#pragma once

// Bronze steg 1 (docs/bronze.md): det härledda flödet, utan dom. Adaptern
// översätter en funktionskropp till en liten flödesgraf (FlowGraph); kärnan
// räknar ut vad varje utdata kan bero på och vad som skrivs garanterat, och
// sätter ihop sammanfattningarna över anrop och översättningsenheter.
//
// Platser är vägar från en parameter eller en lokal: "price", "result.total",
// "this.total", "v[*]" (något element: varje skrivning dit är svag, det gamla
// innehållet finns kvar). Ingångsvärdet av en plats heter som platsen. Ett
// fältsteg kan bära sin klass, "total@Totals": identiteten, medan utskriften
// visar "total" (eller "Totals::total" om två fält annars ser lika ut).

#include <map>
#include <set>
#include <string>
#include <vector>

namespace pork {

// En källa i ett uttryck: en plats som läses eller resultatet av ett anrop.
struct FlowValue {
  enum class Kind { place, call };
  Kind kind = Kind::place;
  std::string place;
  unsigned call = 0;  // FlowCall::id

  static FlowValue read(std::string p) { return {Kind::place, std::move(p), 0}; }
  static FlowValue result(unsigned id) { return {Kind::call, {}, id}; }
  bool operator==(const FlowValue&) const = default;
};
using FlowValues = std::vector<FlowValue>;

// Ett anrop. Argumenten står i den anropade funktionens parameterordning;
// för en metod är argument 0 objektet (`this`).
struct FlowCall {
  unsigned id = 0;
  std::string usr;  // tom: okänd kod, konservativt
  std::string name;
  std::vector<FlowValues> args;   // argumentets värde (för en referens: platsens innehåll)
  std::vector<std::string> refs;  // platsen en skrivbar referens binds till, "" annars
  std::vector<FlowValues> refIndex;  // per referens: indexet när platsen är ett element (v[i])
  bool operator==(const FlowCall&) const = default;
};

struct FlowStmt {
  enum class Kind { assign, weak, call, ret };
  Kind kind = Kind::assign;
  std::string target;  // assign, weak (weak: det gamla värdet kan finnas kvar)
  FlowValues values;   // assign, weak, ret
  FlowCall call;       // call
  std::string note;    // BRZ-7: satsen i meddelanden ("the call of 'f' at line 14"); tom: dold, rapporteras inte
  bool operator==(const FlowStmt&) const = default;
};

struct FlowBlock {
  std::vector<FlowStmt> stmts;
  FlowValues cond;              // villkoret som väljer efterföljare
  std::vector<unsigned> succs;  // index i FlowGraph::blocks
  std::string condNote;         // BRZ-7: "the condition at line 5"
  bool operator==(const FlowBlock&) const = default;
};

// Bronze (spec/3_bronze.txt): PORK_DEPENDS(output, allowed...) eller
// PORK_DEPENDS_RESULT(allowed...) (output "return"). error: kravet själv är
// felskrivet (då håller det inte).
struct FlowRequirement {
  std::string output;
  std::vector<std::string> allowed;
  std::string error;
  bool operator==(const FlowRequirement&) const = default;
};

struct FlowGraph {
  std::vector<FlowBlock> blocks;
  unsigned entry = 0;
  unsigned exit = 0;  // normal retur
  // Parametrarna i ordning (`this` först för en metod) och vilka som är
  // utdata: icke-const referenser, `this` i en icke-const metod.
  std::vector<std::string> params;
  std::vector<bool> outputs;
  bool returnsValue = false;
  std::string unsupported;  // inte tom: flödet räknas inte ut, och varför
  std::vector<FlowRequirement> requirements;
  bool operator==(const FlowGraph&) const = default;
};

// Det härledda flödet för en funktion, uttryckt i dess parametrar.
struct FlowSummary {
  bool known = false;      // false: okänd kod (konservativ vid anrop)
  std::string why;         // !known: varför
  std::vector<std::string> params;  // som i FlowGraph (för att översätta vid anrop)
  // utdata ("return" eller en plats under en utdataparameter) -> ingångsvärden
  std::map<std::string, std::set<std::string>> deps;
  std::set<std::string> always;  // platser som skrivs på varje normal returväg
  // Verkan utanför utdata: anrop av okänd kod (eller av en funktion med sådan
  // verkan), eller ett kast. Ett sådant anrop har alltid verkan (BRZ-7).
  bool external = false;
  // BRZ-7: satser och villkor vars värde inte når någon utdata. Räknas efter
  // fixpunkten (inte en del av den).
  std::vector<std::string> noEffect;
  bool operator==(const FlowSummary&) const = default;
};

// Ett flöde per funktion (usr). Funktioner utan graf (betrodda, ingen
// definition) är okända. Anrop använder den anropades sammanfattning;
// rekursion löses med fixpunkt.
std::map<std::string, FlowSummary> computeFlows(const std::map<std::string, FlowGraph>& graphs);

// BRZ-7: fyller FlowSummary::noEffect för varje funktion med känt flöde.
void findNoEffect(const std::map<std::string, FlowGraph>& graphs, std::map<std::string, FlowSummary>& flows);

// En funktion för sig, med givna sammanfattningar för de anropade.
FlowSummary summarize(const FlowGraph& g, const std::map<std::string, FlowSummary>& callees);

// BRZ-2: tomt om kravet håller mot flödet, annars varför inte
// ("audit depends on discount, which PORK_DEPENDS does not allow").
std::string checkRequirement(const FlowSummary& s, const FlowRequirement& r);

// Raderna i rapporten: "return <- discounted, price",
// "audit <- price (always written)".
std::vector<std::string> flowLines(const FlowSummary& s);

}  // namespace pork
