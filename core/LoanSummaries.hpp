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
//             o.d., `<const>` = varje const-metod utan egen regel), eller
//             kvalificerat för en enskild mall: `std::map::operator[]`.
//             Parametrarnas form efter namnet, utan blanksteg:
//               (iterator)           en parameter, en iterator
//               (iterator,iterator)  två parametrar, båda iteratorer
//               (iterator,...)       första parametern är en iterator
//             Utan form: alla överlagringar.
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
  std::string origin;  // "std.txt:12", för felsökning
  // interval(i,j): parametrarna i och j är ett intervall [first, last) i
  // samma objekt (STN-77); i och j får överlappa varandra.
  std::vector<std::pair<unsigned, unsigned>> intervals;
};

class LoanSummaries {
 public:
  enum class Category { owner, pointer, free };
  // Parameterform: de första `iterators` parametrarna är iteratorer; utan
  // `more` har funktionen exakt så många parametrar. `name` utan form är
  // {0, true}. `erase(iterator)` = {1, false}, `move(iterator,...)` = {1, true},
  // `transform(iterator,iterator,iterator,iterator,iterator,...)` = {5, true}.
  struct Shape {
    unsigned iterators = 0;
    bool more = true;
    auto operator<=>(const Shape&) const = default;
  };

  // Ett anrop: kategori, kvalificerat mallnamn (`std::map::insert`), namn
  // (`insert`), vilka parametrar som är iteratorer, const-metod.
  struct Query {
    Category category;
    std::string qualified;
    std::string name;
    std::vector<bool> iteratorParams;
    bool isConst = false;
  };

  // Mest specifik först: kvalificerat namn före namn före `<const>`, och
  // inom dem en exakt form före en öppen, fler iteratorer före färre, ingen
  // form sist. nullopt: ingen regel (standarden).
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
