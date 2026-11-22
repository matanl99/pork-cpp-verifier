#pragma once

#include "Ports.hpp"

#include <atomic>
#include <string>

namespace pork {

// Bevisare som sparar svaren från en annan bevisare på disk.
//
// Nyckeln är SHA-256 av inner.configuration() och programmet. Programmet
// innehåller funktionen, allt den anropar, typerna och kontrakten, så en
// ändring i något av dem ger en ny nyckel; konfigurationen täcker
// bevisarens version, flaggor, gränser och kontraktsfilernas innehåll.
//
// Sparas: bevisat, fynd och timeouts. Tidsgränsen ingår inte i nyckeln: ett
// svar (bevisat eller fynd) gäller oavsett gräns. En timeout sparas med sin
// gräns och återanvänds bara när den nya gränsen inte är större. Ett snabbt
// pass (kort --silver-timeout) följt av ett långsamt (lång) kör alltså bara om
// det som inte hann klart. Övriga fel (bevisaren saknas, kraschade) sparas inte.
//
// En fil per svar, skriven till en temporärfil och flyttad på plats
// (rename), så parallella prove() och samtidiga pork-körningar går bra.
class CachingProver : public IProver {
 public:
  struct Options {
    std::string dir;           // katalogen för svaren (skapas vid behov)
    bool retryTimeouts = false;  // --silver-slow: pröva sparade timeouts igen
  };

  CachingProver(IProver& inner, Options options) : inner_(inner), options_(std::move(options)) {}

  ProofResult prove(const std::string& program) override;
  std::string configuration() const override { return inner_.configuration(); }
  unsigned timeoutSeconds() const override { return inner_.timeoutSeconds(); }

  unsigned hits() const { return hits_; }
  unsigned misses() const { return misses_; }
  // Svar som är timeouts (nya eller sparade) i den här körningen.
  unsigned timeouts() const { return timeouts_; }

  // Nyckeln för ett program (64 hex-tecken). Exponerad för tester.
  std::string key(const std::string& program) const;

 private:
  IProver& inner_;
  Options options_;
  std::atomic<unsigned> hits_ = 0;
  std::atomic<unsigned> misses_ = 0;
  std::atomic<unsigned> timeouts_ = 0;
};

}  // namespace pork
