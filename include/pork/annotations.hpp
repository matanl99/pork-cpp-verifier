#pragma once

// Annoteringar för pork. Inkludera i koden som ska analyseras.
//
//   PORK_STONE int f(int a);    // f måste vara Stone (annars error, exit 1)
//   PORK_TRUSTED int g(int a);  // g räknas som Stone utan analys; anropare
//                               // får "stone: assuming g"
//
//   int half(int a) {
//     PORK_PRE(a >= 0);                    // förvillkor (som SPARK Pre)
//     PORK_POST(int r, r >= 0 && r <= a);  // eftervillkor (som SPARK Post)
//     return a / 2;
//   }
//
//   void bump(int& x) {
//     PORK_PRE(x < 100);
//     PORK_POST(void, x <= 100);           // void-funktion: inget resultat
//     x = x + 1;
//   }
//
// Kontrakten står först i kroppen. I PORK_POST är en parameter som tas som
// värde eller const& ingångsvärdet, en T&-parameter slutvärdet.
//
// Båda kompileras normalt bort (villkoren typkontrolleras men körs inte).
// När pork analyserar (PORK_ANALYSIS definierat) blir de anrop till
// pork::pre/pork::post. Silver antar förvillkoret och bevisar eftervillkoret
// i funktionen; en anropare kontrollerar förvillkoret och använder sedan
// bara eftervillkoret (funktionens kod tas inte med: modulärt bevis).
//
// Typinvariant (som SPARK Type_Invariant), i en struct:
//
//   struct Range {
//     int lo, hi;
//     PORK_INVARIANT(lo <= hi);
//   };
//
// Blir metoden `bool pork_invariant() const`, som även går att anropa
// (t.ex. i tester). Silver lägger till den vid funktionsgränser efter typ:
// en parameter av typen (värde, const& eller &) och *this är förvillkor;
// en T&-parameter, *this i en icke-const-metod och ett returvärde av typen
// är dessutom eftervillkor. Funktioner som invarianten själv anropar
// (transitivt) undantas. Inuti en funktion får invarianten brytas tillfälligt.
//
// Kontrakt för ett gränssnitt (som SPARK:s Pre'Class/Post'Class) står i en
// egen kontraktsfil, en C++-fil som analyseras med de andra. Per metod en fri
// funktion med samma namn, gränssnittet som första parameter (const I& för en
// const-metod, annars I&) och sedan metodens parametrar, med bara
// PORK_PRE/PORK_POST i kroppen:
//
//   #pragma clang diagnostic ignored "-Wreturn-type"
//   PORK_INTERFACE_CONTRACT int now(const IClock& self) { PORK_POST(int r, r >= 0); }
//
// Ett dispatchande anrop kontrollerar förvillkoret och använder
// eftervillkoret; varje känd implementation bevisas uppfylla kontraktet.
//
// Andra kompilatorer än clang ser tomma makron.

#if defined(__clang__)
#define PORK_STONE [[clang::annotate("pork::stone")]]
#define PORK_TRUSTED [[clang::annotate("pork::trusted")]]
#define PORK_INTERFACE_CONTRACT [[clang::annotate("pork::interface_contract")]]
#else
#define PORK_STONE
#define PORK_TRUSTED
#define PORK_INTERFACE_CONTRACT
#endif

#if defined(PORK_ANALYSIS)
namespace pork {
[[clang::annotate("pork::pre")]] constexpr void pre(bool) noexcept {}
template <typename F>
[[clang::annotate("pork::post")]] constexpr void post(const F&) noexcept {}
template <typename... T>
[[clang::annotate("pork::depends")]] constexpr void depends(const T&...) noexcept {}
template <typename... T>
[[clang::annotate("pork::depends_result")]] constexpr void depends_result(const T&...) noexcept {}
}  // namespace pork
#define PORK_PRE(...) ::pork::pre(__VA_ARGS__)
// Bronze (spec/3_bronze.txt): utdatan får bara bero på de tillåtna platserna.
#define PORK_DEPENDS(output, ...) ::pork::depends(output __VA_OPT__(, ) __VA_ARGS__)
#define PORK_DEPENDS_RESULT(...) ::pork::depends_result(__VA_ARGS__)
#define PORK_POST(result, ...) ::pork::post([&](result) -> bool { return (__VA_ARGS__); })
#define PORK_INVARIANT(...)                                                              \
  [[clang::annotate("pork::invariant")]] bool pork_invariant() const { return (__VA_ARGS__); } \
  static_assert(true)
#else
#define PORK_PRE(...) static_cast<void>(sizeof(static_cast<bool>(__VA_ARGS__)))
namespace pork::detail {
template <typename... T>
int names(const T&...);  // bara i sizeof: kompilatorn kontrollerar namnen, ingen kod
}  // namespace pork::detail
#define PORK_DEPENDS(output, ...) static_cast<void>(sizeof(::pork::detail::names(output __VA_OPT__(, ) __VA_ARGS__)))
#define PORK_DEPENDS_RESULT(...) static_cast<void>(sizeof(::pork::detail::names(__VA_ARGS__)))
#define PORK_POST(result, ...) static_cast<void>([&](result) -> bool { return (__VA_ARGS__); })
#define PORK_INVARIANT(...)                                     \
  bool pork_invariant() const { return (__VA_ARGS__); } \
  static_assert(true)
#endif
