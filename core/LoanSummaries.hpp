#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace pork {

// Sammanfattningar av biblioteksfunktioner för lånemodellen (STN-62,
// docs/stone_model.md), som data: det vi litar på utan bevis om std, i en
// fil som går att granska mot standarden. Inbyggd: summaries/std.txt; mer
// eller ersättande regler med --summaries.
//
// En rad:  <kategori> <namn>... : <effekt> [modifierare...]
//
//   kategori  owner    metod på en std-typ som clang märker [[gsl::Owner]]
//                      (behållare, optional, variant, string)
//             pointer  metod på en std-typ märkt [[gsl::Pointer]] (iteratorer, vyer)
//             free     fri std-funktion, eller std::ranges-algoritm (niebloid)
//   namn      metodens namn (`find`, `operator[]`, `conversion` = operator bool
//             o.d., `<const>` = varje const-metod utan egen regel, `<ctor>` =
//             en konstruktor utan egen regel), eller
//             kvalificerat för en enskild mall: `std::map::operator[]`.
//             Parametrarnas form efter namnet, utan blanksteg:
//               (iterator)           en parameter, en iterator
//               (iterator,iterator)  två parametrar, båda iteratorer
//               (iterator,...)       första parametern är en iterator
//               (range,...)          första parametern är ett intervall (en
//                                    niebloid: std::ranges::copy(r, out))
//             Utan form: alla överlagringar utom intervallformer av niebloider.
//   effekt    default      ingen sammanfattning: den försiktiga standarden
//             reads        läser, skriver ingenting (inte heller genom lån)
//             access       som reads; resultatet lånar ägd lagring i objektet (eller arg0)
//             self         skriver objektets egen lagring (iterator++), inte genom dess lån
//             writeAccess  skriver objektet; resultatet lånar ägd lagring i det
//             identity     resultatet är argumentet självt (std::move)
//             insert       skriver objektet men befintliga element och iteratorer
//                          förblir giltiga (nodbaserade behållare); resultatet
//                          lånar ägd lagring i objektet
//   modifierare
//             position  första argumentet (en iterator) får peka in i objektet
//             range     alla iteratorargument får peka in i objektet
//             consumes  iteratorn i första argumentet blir ogiltig; tillåtet
//                       bara som `it = c.f(it)` (den skrivs över direkt)
//             forwards  en T&&-parameter (eller objektet i en &&-metod) skickas
//                       bara vidare: resultatet är en referens in i den, och
//                       inget tas i anropet. Flytten sker där resultatet
//                       används (och fångas där, P-d).
//             copies    ett argument som är en const T&-parameter får referera in
//                       i objektet: funktionen läser värdet innan den ändrar
//                       objektet (v.push_back(v[0]), [res.on.arguments] och
//                       LWG 526; inte assign(n, t), som har förvillkoret att t
//                       inte refererar in i a)
//             resultIn(i)  resultatet är en position i det parameter i pekar
//                       in i, och lånar inget annat ([alg.remove], [alg.unique]:
//                       slutet på det nya intervallet). Bara för iteratorer.
//             nonEmpty  objektet får inte vara tomt (std::unique_ptr::operator*, STN-89)
//             resultNonEmpty  resultatet är inte tomt (std::make_unique)
//             resultNullIfEmpty  resultatet är en nullpekare om objektet är tomt (get)
//             keepsElements  inga nya element och inga befintliga töms (erase, clear)
//             insertsArguments  nya element konstrueras ur argumenten, befintliga
//                       behålls (push_back); utan argument är det nya elementet tomt
//             swaps     de två operanderna byter innehåll (swap)
//             keepsValues  befintliga element behåller sina värden; de kan
//                       flyttas till ny lagring men förstörs inte
//             interval(i,j)  parameter i och j (räknat från 0, utan objektet)
//                       är ett intervall [first, last): båda pekar in i
//                       samma objekt och får överlappa. Andra iterator-
//                       argument som kan peka in i samma objekt avvisas
//                       om funktionen kan skriva genom dem. En fri
//                       std-funktion utan rad, med två eller fler iterator-
//                       eller pekarargument, avvisas (rollerna okända).
//
// Tomma rader och `#`-kommentarer ignoreras. Samma regel två gånger i en fil
// är ett fel; en senare fil ersätter en tidigare.
enum class LoanEffect { standard, reads, access, self, writeAccess, identity, insert };

struct LoanRule {
  LoanEffect effect = LoanEffect::standard;
  bool position = false;
  bool range = false;
  bool consumes = false;
  bool forwards = false;
  std::string origin;  // "std.txt:12", för felsökning
  // interval(i,j): parametrarna i och j är ett intervall [first, last) i
  // samma objekt (STN-77); i och j får överlappa varandra.
  std::vector<std::pair<unsigned, unsigned>> intervals;
  bool copies = false;
  std::optional<unsigned> resultIn;  // resultIn(i): resultatet pekar dit parameter i pekar
  // Tomhet (STN-89): objektet får inte vara tomt (`*p`), resultatet är inte tomt
  // (make_unique), resultatet är null om objektet är tomt (get), befintliga
  // element behålls (erase), nya element kommer ur argumenten (push_back).
  bool nonEmpty = false;
  bool resultNonEmpty = false;
  bool resultNullIfEmpty = false;
  bool keepsElements = false;
  bool insertsArguments = false;
  bool swaps = false;
  bool keepsValues = false;  // befintliga element behåller sina värden (kan flyttas till ny lagring)  // objektet och argumentet (eller de två argumenten) byter innehåll
};

class LoanSummaries {
 public:
  enum class Category { owner, pointer, free };
  // Parameterns slag i en form: en iterator (eller pekare), eller ett intervall
  // som argument till en niebloid (`std::ranges::copy(r, out)`: r har begin()).
  enum class Param { other, iterator, range };
  // Parameterform: de första parametrarna har slagen i `params`; utan `more`
  // har funktionen exakt så många parametrar. `name` utan form är {{}, true}.
  // `erase(iterator)` = {{iterator}, false}, `move(iterator,...)` =
  // {{iterator}, true}, `copy(range,...)` = {{range}, true}.
  struct Shape {
    std::vector<Param> params;
    bool more = true;
    auto operator<=>(const Shape&) const = default;
  };

  // Ett anrop: kategori, kvalificerat mallnamn (`std::map::insert`), namn
  // (`insert`), parametrarnas slag, const-metod.
  struct Query {
    Category category;
    std::string qualified;
    std::string name;
    std::vector<Param> params;
    bool isConst = false;
    bool isConstructor = false;
  };

  // Mest specifik först: kvalificerat namn före namn före `<const>`, och
  // inom dem en exakt form före en öppen, fler parametrar före färre, ingen
  // form sist. Ett anrop med ett intervall bland parametrarna träffar bara en
  // form som nämner `range`: intervallformen av en niebloid ärver aldrig
  // iteratorformens roller. nullopt: ingen regel (standarden).
  std::optional<LoanRule> find(const Query& q) const;

  // Läs regler ur text (`source` i felmeddelanden). Regler ersätter
  // tidigare med samma nyckel; dubbletter inom samma text är ett fel.
  std::expected<void, std::string> parse(std::string_view text, const std::string& source);

  std::size_t size() const { return rules_.size(); }

 private:
  using Key = std::tuple<Category, std::string, Shape>;
  std::map<Key, LoanRule> rules_;
};

// Den inbyggda filen summaries/std.txt (inbyggd vid bygget).
std::string_view stdLoanSummariesText();

// Inbyggda regler plus eventuell egen fil.
std::expected<LoanSummaries, std::string> loadLoanSummaries(const std::string& extraFile = {});

}  // namespace pork
