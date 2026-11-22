#pragma once

#include "Ports.hpp"

#include <atomic>
#include <string>

namespace pork {

// Testramen deklarerar indata oinitierade. Kopieras en unionmedlem i sådana
// indata kan cbmc 6.11.0 ge motexempel som inte stämmer med antagandena
// (--no-simplify och ESBMC godkänner samma program; med indata från en
// funktion utan kropp godkänner cbmc det också). Att läsa oinitierade lokala
// variabler är odefinierat beteende i C, så detta är en tillfällig lösning
// tills testramen skapar indata på det sättet. Huvudbevisaren kör allt som förut. Hittar den fel i ett
// program med en union (std::variant i C) körs samma program med
// kontrollbevisaren (ESBMC när den finns, annars cbmc --no-simplify):
// godkänner den programmet gäller det, annars står huvudbevisarens svar kvar.
// Bevisarfel och tidsgränser är inga motexempel och kontrolleras inte.
// Avbryts kontrollen (tidsgräns, körningsfel) är svaret ett bevisarfel med
// huvudbevisarens fynd i texten: inget slutresultat, så cachen sparar det
// som en timeout (prövas igen med längre gräns) eller inte alls.
// Skyddar mot falska motexempel, inte mot ett felaktigt bevis.
class UnionCheckProver : public IProver {
 public:
  // checkName: i meddelandet när kontrollen avbryts ("esbmc").
  UnionCheckProver(IProver& main, IProver& check, std::string checkName = "the union check")
      : main_(main), check_(check), checkName_(std::move(checkName)) {}

  // Programmet innehåller nyckelordet union (som ord, inte i ett namn).
  static bool hasUnion(const std::string& program);

  ProofResult prove(const std::string& program) override;
  // Båda bevisarnas konfiguration: cachen skiljer på dem.
  std::string configuration() const override;
  unsigned timeoutSeconds() const override { return main_.timeoutSeconds(); }

  unsigned checked() const { return checked_; }        // fel som kontrollerades
  unsigned overturned() const { return overturned_; }  // av dem: godkända av kontrollen

 private:
  IProver& main_;
  IProver& check_;
  std::string checkName_;
  std::atomic<unsigned> checked_ = 0;
  std::atomic<unsigned> overturned_ = 0;
};

}  // namespace pork
