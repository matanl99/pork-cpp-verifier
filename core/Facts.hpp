#pragma once

#include "Flow.hpp"

#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pork {

// En "reason" är varför något INTE är Stone. nullopt betyder godkänt.
using Reason = std::optional<std::string>;

// En anropad funktion, som anroparen ser den.
// Ett dispatchande anrop (virtuellt, kan nå vilken override som helst)
// registreras under "dispatch:" + usr för den anropade metoden.
inline constexpr std::string_view kDispatchPrefix = "dispatch:";

struct Callee {
  std::string name;           // med templateargument, för utskrift: "std::clamp<float>"
  std::string qualifiedName;  // utan templateargument, matchas mot trust-listan och
                              // redovisas som antagande: "std::clamp", "std::optional::reset"
  bool trusted = false;       // deklarationen är märkt pork::trusted
  std::string alias;          // ett annat namn som trust-listan också kan betro:
                              // "sqrt" för "std::sqrt" (glibc:s ::sqrt via using i std)
};

// Aliasfrågor mellan en port och dess implementationer (STN-55), som inte går
// att avgöra i en TU: implementationerna kan ligga i andra. Typer anges med
// sitt kanoniska namn; `reach` är typerna som ett objekt av typen kan
// innehålla (typen själv, fält, baser, typargument, transitivt).
struct RefShape {        // en kontextreferens (STN-53) i en klass
  std::string field;     // "Logger::text_"
  std::string type;
  std::set<std::string> reach;
  bool isConst = false;
};
struct ArgShape {        // ett referensargument i ett dispatchande anrop
  std::string text;      // som i källan
  std::string type;
  std::set<std::string> reach;
  bool isConst = false;
  // Vägen börjar i *this och går bara genom värdefält: objekten som
  // innehåller argumentet är kända (typerna längs vägen). Annars okända.
  bool known = false;
  std::set<std::string> containers;
};
struct DispatchCall {
  std::string usr;       // den anropade metoden (utan kDispatchPrefix)
  std::vector<ArgShape> args;
};

// Silver: en funktion översatt till C (för CBMC). Varje funktion översätts för
// sig; kärnan sätter ihop programmet för en funktion och allt den anropar.
// Självtest av förvillkoren: ett eget program som antar dem och sedan har
// __CPROVER_assert(0, kSatisfiableMarker). Kan förvillkoren gälla misslyckas
// den (som väntat); håller den kan de aldrig gälla, och då är varje bevis tomt.
// Eget program: i samma program som beviset gjorde den cbmc mångdubbelt långsammare.
inline constexpr const char* kSatisfiableMarker = "pork: preconditions are satisfiable";

// Beroendeoraklet (--flow-oracle, docs/bronze.md steg 3): ett löv är en del av
// en indata eller utdata som platsmodellen i --flow namnger ("t.total@Totals",
// "v[*]", "return"). assume/compare: C-villkor för att körning a och b har lika
// värde där (assume över hela lagringen, compare bara över det som finns).
struct OracleLeaf {
  std::string path;
  std::string assume;
  std::string compare;
  bool output = false;  // utdata: T&-parameter, *this i icke-const metod, resultatet
};
struct FlowOracleHarness {
  std::string setup;  // indata för körning a och b (godtyckliga, giltiga)
  std::string run;    // båda anropen av __entry (förvillkoren antas), resultat pork_ra/pork_rb
  std::vector<OracleLeaf> leaves;
};

struct CFunction {
  std::string name;  // C-namnet
  // Typer och kontraktsprototyper som definitionen behöver, i beroendeordning.
  // Nyckeln gör att samma typ från flera funktioner bara tas med en gång.
  std::vector<std::pair<std::string, std::string>> decls;
  std::string prototype;
  std::string definition;  // PORK_PRE kontrolleras (så anropas funktionen)
  std::string entry;       // PORK_PRE antas, plus pork_harness() som anropar med godtyckliga indata
  std::string satisfiable;  // självtest (eget program): förvillkoren antas, sedan kSatisfiableMarker; tomt = inga förvillkor
  std::map<std::string, std::string> contracts;  // C-namn på anropade kontrakt -> C++-namn
  std::set<std::string> assumptions;  // modellens gränser, t.ex. "std::vector size <= 16"
  // Anrop till egna funktioner som inte är definierade i den här TU:n: C-namn
  // -> kontraktets C-namn (tomt om inget kan finnas, t.ex. för metoder).
  // Silver tar med översättningen från en annan TU om den finns, annars kontraktet.
  std::map<std::string, std::string> externals;
  // Med PORK_POST: en version med samma namn och signatur som bara
  // kontrollerar förvillkoret och returnerar ett godtyckligt resultat som
  // uppfyller eftervillkoret (T&-parametrar och *this får godtyckliga nya
  // värden). Anropare använder den i stället för koden. Tom = ingen POST.
  std::string contract;
  std::set<std::string> contractCallees;  // USR för funktioner som PRE/POST/invarianter anropar
  // Gränssnittskontrakt (kontraktsfilen): `_Bool <name>__pre(...)` och
  // `_Bool <name>__post([resultat,] ...)` för Liskov-bevisen. Tomt annars.
  std::string predicates;
  // Virtuell metod: per metod den uppfyller kontraktet för (sig själv och de
  // den överskuggar, usr) en pork_harness som antar kontraktets förvillkor,
  // anropar metoden och kontrollerar kontraktets eftervillkor.
  std::map<std::string, std::string> lsp;
  // Ärvda förvillkor (som SPARK:s Pre'Class): makronamn -> metod (usr) vars
  // gränssnittskontrakt gäller. Kärnan definierar makrot som kontraktets
  // __pre, eller som 1 om metoden saknar kontrakt.
  std::map<std::string, std::string> inherits;
  bool inheritsOnly = false;  // alla förvillkor är ärvda (självtestet behövs bara om något kontrakt finns)
  std::optional<FlowOracleHarness> oracle;  // nullopt: oracleWhy
  std::string oracleWhy;
};

// Det som samlas in per funktionsdefinition, oberoende av andra funktioner.
// Om funktionen är Stone avgörs först när alla översättningsenheter är lästa
// (se Classify.hpp), eftersom det beror på vad den anropar.
struct FunctionFacts {
  std::string usr;   // Unified Symbol Resolution: samma nyckel i alla TU:er
  // STN-94: definitionen efter preprocessorn (clangs ODRHash). Samma funktion
  // (en inline-funktion i en header) ska ha samma definition i varje TU.
  unsigned definitionHash = 0;
  std::string name;  // kvalificerat namn med templateargument, för utskrift
  std::string file;
  unsigned line = 0;
  unsigned column = 0;
  Reason local;                          // brott mot delmängden i själva funktionen
  // --loans=report: lånemodellens dom (Reason: tom = godkänd) och om
  // reglerna (LocalCheck) avvisade, var för sig. nullopt = inte rapporterat.
  // Stone-domen (`local`) innehåller alltid båda.
  std::optional<Reason> loans;
  bool rulesRejected = false;
  std::map<std::string, Callee> callees;  // usr -> anropad funktion, direkta anrop
  bool trusted = false;                  // märkt pork::trusted: kroppen analyseras inte
  bool required = false;                 // märkt pork::stone: måste bli Stone
  bool declaresDepends = false;          // PORK_DEPENDS någonstans i funktionen (BRZ-6)
  bool misplacedDepends = false;         // ett PORK_DEPENDS utanför kontraktsinledningen
  // Förekomsterna (StrayRequirement::occurrence) för kraven utanför
  // inledningen: rapporterar funktionen ett kravbrott släpps mallmönstrets fel
  // för samma förekomst.
  std::set<std::string> misplacedAt{};
  std::set<std::string> assumedTypes;    // betrodda typer som används ("type std::optional")
  std::set<std::string> overrides;       // virtuella metoder som denna överskuggar (usr, transitivt)
  // Metod: klassens form (STN-55). fieldReach = typer som fälten (även
  // basernas) kan innehålla, contextRefs = kontextreferenserna, hasPorts =
  // klassen har portreferenser (kan anropa tillbaka).
  std::set<std::string> fieldReach;
  std::vector<RefShape> contextRefs;
  bool hasPorts = false;
  bool constMethod = false;
  std::vector<DispatchCall> dispatchCalls;  // dispatchande anrop med referensargument
  std::string contractFor;    // gränssnittskontrakt (PORK_INTERFACE_CONTRACT): metodens usr
  bool declContract = false;  // metod med egna PORK_PRE/PORK_POST i kroppen
  // --flow (Bronze steg 1): kroppen som flödesgraf. nullopt = inte begärd/betrodd.
  std::optional<FlowGraph> flow;
  // Silver: C-översättningen eller varför den inte gick. nullopt = inte översatt.
  std::optional<std::expected<CFunction, std::string>> c;
};

using FactsMap = std::map<std::string, FunctionFacts>;  // usr -> fakta

// BRZ-6: ett PORK_DEPENDS utanför varje funktionskropp (fältinitierare,
// initierare för en global, defaultargument). Det hör inte till någon
// funktion och håller aldrig.
struct StrayRequirement {
  std::string file;
  unsigned line = 0;
  unsigned column = 0;
  std::string where;  // "the initializer of field 'Cfg::b'"
  std::string problem = "is outside a function body";
  // Ett felplacerat krav i ett mallmönster ("must come before all other
  // statements"). Släpps bara om en analyserad instans rapporterar samma
  // plats (FunctionFacts::misplacedAt); en bortvald if constexpr-gren gör det
  // inte.
  bool inTemplate = false;
  // Förekomstens identitet: mallens usr (för ett mallmönster), den verkliga
  // filen och byteoffseten, inte visningsläget (#line kan ge två förekomster
  // samma fil, rad och kolumn).
  std::string occurrence{};
  auto operator<=>(const StrayRequirement&) const = default;
};

// Namn (utan templateargument) på funktioner som räknas som Stone utan analys
// när de saknar definition i de analyserade källorna. Motsvarar SPARK:s
// specifikationer vars kroppar inte är i SPARK: antaganden, inte bevis.
//
// `names` kan innehålla jokertecken: "std::optional::*" betror alla medlemmar.
// `types` är klasstemplates vars instanser är värdetyper när alla typargument
// är det ("std::optional").
struct TrustList {
  std::set<std::string> names;
  std::set<std::string> types;

  bool trustsFunction(const std::string& qualifiedName) const {
    if (names.contains(qualifiedName)) return true;
    for (const auto& n : names)
      if (n.ends_with("::*") && qualifiedName.starts_with(n.substr(0, n.size() - 1))) return true;
    return false;
  }
};

// Slutligt besked för en funktion.
struct Verdict {
  enum class Kind { silver, stone, trusted, notStone };

  Kind kind = Kind::stone;
  std::string reason;                 // notStone: varför; stone: varför inte silver ("" = ej prövad)
  std::set<std::string> assumptions;  // stone/silver: betrodda funktioner den vilar på (transitivt)

  std::string modelInputs{};
  std::vector<std::string> flow{};  // --flow: härlett flöde för en Stone-funktion (ingen dom)
  std::vector<std::string> flowOracle{};  // --flow-oracle: beroendeoraklets utfall
  // Bronze (spec/3_bronze.txt): nullopt = inga flödeskrav, "" = kraven håller,
  // annars varför inte (då är funktionen inte silver).
  std::optional<std::string> bronze{};
  // Bevisad fri från körtidsfel men inte silver för att ett flödeskrav inte
  // håller (BRZ-4). Oraklet prövar flödet ändå.
  bool runtimeProved = false;

  static Verdict stone() { return {}; }
  static Verdict trust() { return {Kind::trusted, {}, {}}; }
  static Verdict notStone(std::string why) { return {Kind::notStone, std::move(why), {}}; }

  // Stone eller bättre.
  bool isStone() const { return kind == Kind::stone || kind == Kind::silver; }

  bool operator==(const Verdict&) const = default;
};

}  // namespace pork
