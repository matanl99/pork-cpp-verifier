// Range-adaptrar (std::views::filter, reverse, as_rvalue) är inte Stone: deras
// typer och uttryck finns inte i tillåtelselistan, också när vytyperna står i
// trust-filen (en filter_view med en lambda är ingen värdetyp). Fallen är
// gränsfall som ett stöd för adaptrar måste ta ställning till (IDEAS.md,
// "Range-adaptrar"):
//  - filter_view::begin() är inte const: första anropet sparar positionen i
//    vyn, och den överlever ändringar som inte ogiltigförklarar den.
//  - Att ändra ett element genom en filter-iterator så att predikatet inte
//    längre håller är odefinierat ([range.filter.iterator]).
//  - Flytt genom en filtrerad vy prövar predikatet på flyttade värden, och en
//    bakåtsökning kan gå före början (P3725, UC1).

#include <list>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using Words = std::vector<std::string>;

// Väldefinierat: den sparade positionen (4) ligger kvar efter push_front, så
// en återanvänd vy börjar vid 4 fast en ny börjar vid 2.
// rules: STN-14, STN-4
// expect: not-stone: local variable 'reused' has type
int cached_begin() {
  std::list<int> numbers{1, 4, 8};
  auto even = [](int n) { return n % 2 == 0; };
  auto reused = numbers | std::views::filter(even);
  const int before = *reused.begin();
  numbers.push_front(2);
  auto fresh = numbers | std::views::filter(even);
  return before == 4 && *reused.begin() == 4 && *fresh.begin() == 2 ? 0 : 1;
}

// Odefinierat: varje träff blir udda när den ökas.
// rules: STN-35
// expect: not-stone: range-for over
void bump_matches(std::vector<int>& v) {
  auto even = [](int n) { return n % 2 == 0; };
  for (int& n : v | std::views::filter(even)) ++n;
}

// Samma sak utan vy: villkoret prövas en gång per element.
// rules: STN-35
// expect: stone: assuming
void bump_matches_plain(std::vector<int>& v) {
  for (int& n : v)
    if (n % 2 == 0) ++n;
}

// rules: STN-18
// expect: not-stone: expression of type
Words move_through_reversed_filter(Words& words) {
  auto long_word = [](const std::string& word) { return word.size() > 5; };
  return words | std::views::filter(long_word) | std::views::reverse | std::views::as_rvalue |
         std::ranges::to<Words>();
}

// Ofarligt (längden prövas före flytten), men både reverse_view och flytten
// ur ett element genom en referens (STN-57) avvisas.
// rules: STN-35, STN-57
// expect: not-stone: range-for over
Words move_long_words_backwards(Words& words) {
  Words result;
  for (auto& word : words | std::views::reverse)
    if (word.size() > 5) result.push_back(std::move(word));
  return result;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"cached_begin", [] { return cached_begin(); }},
    {"bump_matches_plain", [] { std::vector<int> v{1, 4, 7, 10}; bump_matches_plain(v); return v == std::vector<int>{1, 5, 7, 11} ? 0 : 1; }},
    {"move_long_words_backwards", [] {
       Words w{"Amsterdam", "Berlin", "Cologne", "LA"};
       return move_long_words_backwards(w) == Words{"Cologne", "Berlin", "Amsterdam"} ? 0 : 1;
     }})
#endif
