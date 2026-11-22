#include "TextReport.hpp"

namespace pork {

namespace {

void where(std::ostream& os, const FunctionFacts& f) {
  os << f.file << ':' << f.line << ':' << f.column << ": ";
}

void verdictText(std::ostream& os, const Verdict& v) {
  switch (v.kind) {
    case Verdict::Kind::silver:
    case Verdict::Kind::stone: {
      bool silver = v.kind == Verdict::Kind::silver;
      bool notBronze = v.bronze && !v.bronze->empty();
      // BRZ-4: bronze = Stone med flödeskrav som håller.
      os << (silver ? "silver" : v.bronze && !notBronze ? "bronze" : "stone");
      bool said = false;
      if (notBronze) {
        os << ": not bronze: " << *v.bronze;
        said = true;
      }
      if (!silver && !v.reason.empty()) {
        os << (said ? "; not silver: " : ": not silver: ") << v.reason;
        said = true;
      }
      const char* sep = !said || silver ? ": assuming " : "; assuming ";
      for (const auto& a : v.assumptions) {
        os << sep << a;
        sep = ", ";
      }
      break;
    }
    case Verdict::Kind::trusted:
      os << "trusted";
      break;
    case Verdict::Kind::notStone:
      os << "not-stone: " << v.reason;
      break;
  }
}

}  // namespace

void TextReport::function(const FunctionFacts& f, const Verdict& v) {
  where(out_, f);
  out_ << f.name << ": ";
  verdictText(out_, v);
  out_ << '\n';
  if (f.loans) {
    out_ << "  loans: ";
    if (*f.loans) out_ << "rejected: " << **f.loans;
    else out_ << "ok";
    out_ << (f.rulesRejected ? "; local: rejected\n" : "; local: ok\n");
  }
  for (const std::string& line : v.flow) out_ << "  flow: " << line << '\n';
  for (const std::string& line : v.flowOracle) out_ << "  flow oracle: " << line << '\n';
  if (!v.modelInputs.empty()) out_ << "  counterexample (scalar inputs in Silver's model):\n" << v.modelInputs;
}

void TextReport::strayRequirement(const StrayRequirement& s) {
  err_ << s.file << ':' << s.line << ':' << s.column << ": error: PORK_DEPENDS in " << s.where << ' '
       << s.problem << '\n';
}

void TextReport::violation(const FunctionFacts& f, const Verdict& v) {
  if (v.bronze && !v.bronze->empty()) {  // BRZ-6
    where(err_, f);
    err_ << "error: '" << f.name << "' does not meet PORK_DEPENDS: " << *v.bronze << '\n';
  }
  if (!f.required || v.isStone()) return;  // annars bara PORK_DEPENDS-kravet
  where(err_, f);
  err_ << "error: '" << f.name << "' is marked pork::stone but is ";
  verdictText(err_, v);
  if (v.kind == Verdict::Kind::trusted) err_ << " (body not analysed)";
  err_ << '\n';
}

void TextReport::error(const std::string& message) { err_ << "pork: " << message << '\n'; }

}  // namespace pork
