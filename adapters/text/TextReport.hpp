#pragma once

#include "Ports.hpp"

#include <ostream>

namespace pork {

// Utgående adapter, kompilatorlikt format.
//
// out, en rad per funktion:
//   fil:rad:kolumn: namn: stone
//   fil:rad:kolumn: namn: stone: assuming a, b
//   fil:rad:kolumn: namn: trusted
//   fil:rad:kolumn: namn: not-stone: orsak
// err, brutna krav:
//   fil:rad:kolumn: error: 'namn' is marked pork::stone but is not-stone: orsak
class TextReport : public IReport {
 public:
  TextReport(std::ostream& out, std::ostream& err) : out_(out), err_(err) {}
  void function(const FunctionFacts& f, const Verdict& v) override;
  void violation(const FunctionFacts& f, const Verdict& v) override;
  void strayRequirement(const StrayRequirement& s) override;
  void error(const std::string& message) override;

 private:
  std::ostream& out_;
  std::ostream& err_;
};

}  // namespace pork
