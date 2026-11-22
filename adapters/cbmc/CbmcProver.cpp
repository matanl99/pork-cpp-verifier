#include "CbmcProver.hpp"

#include "ProverProcess.hpp"

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>

#include <optional>

namespace pork {

namespace {

using process::TempFile;
using process::versionOf;

// Flaggorna till cbmc (följt av antalet varv för --unwind).
constexpr const char* kFlags[] = {"--function",           "pork_harness",          "--signed-overflow-check",
                                  "--div-by-zero-check",  "--float-div-by-zero-check", "--undefined-shift-check",
                                  "--float-overflow-check", "--nan-check",          "--pointer-check",
                                  "--unwinding-assertions", "--json-ui",            "--unwind"};

std::string str(const llvm::json::Object& o, llvm::StringRef key) {
  if (auto s = o.getString(key)) return s->str();
  return {};
}

std::string inputValue(const llvm::json::Object& value) {
  if (str(value, "name") != "float") return str(value, "data");
  // CBMC:s decimaltext kan vara avrundad. Återskapa värdet från bitarna.
  std::string bits = str(value, "binary");
  const llvm::fltSemantics* semantics = nullptr;
  if (bits.size() == 32) semantics = &llvm::APFloat::IEEEsingle();
  if (bits.size() == 64) semantics = &llvm::APFloat::IEEEdouble();
  if (bits.size() == 80) semantics = &llvm::APFloat::x87DoubleExtended();
  if (bits.size() == 128) semantics = &llvm::APFloat::IEEEquad();
  if (semantics == nullptr || bits.find_first_not_of("01") != std::string::npos) return {};
  llvm::APFloat number(*semantics, llvm::APInt(bits.size(), bits, 2));
  llvm::SmallString<32> decimal;
  number.toString(decimal);
  return decimal.str().str();
}

std::string modelInputs(const llvm::json::Object& property) {
  const auto* trace = property.getArray("trace");
  if (trace == nullptr) return {};
  std::string inputs;
  for (const auto& entry : *trace) {
    const auto* step = entry.getAsObject();
    if (step == nullptr || str(*step, "stepType") != "input") continue;
    const auto* location = step->getObject("sourceLocation");
    if (location == nullptr || str(*location, "function") != "pork_harness") continue;
    const auto* values = step->getArray("values");
    if (values == nullptr || values->size() != 1) continue;
    const auto* value = values->front().getAsObject();
    if (value == nullptr) continue;
    std::string name = str(*step, "inputID");
    std::string data = inputValue(*value);
    if (!name.empty() && !data.empty()) inputs += "    " + name + " = " + data + "\n";
  }
  return inputs;
}

}  // namespace

std::vector<std::string> CbmcProver::solverFlags(const std::string& solver) {
  static const char* smt[] = {"z3", "cvc4", "cvc5", "bitwuzla", "boolector", "yices", "mathsat"};
  if (solver.empty() || solver == "minisat") return {};
  for (const char* s : smt)
    if (solver == s) return {"--" + solver};
  return {"--external-sat-solver", solver};
}

CbmcProver::CbmcProver(Options options)
    : options_(std::move(options)), solverFlags_(solverFlags(options_.solver)) {}

// cbmc-versionen, flaggorna, minnesgränsen och kontraktsfilernas innehåll (inte tidsgränsen).
std::string CbmcProver::configuration() const {
  std::call_once(configured_, [&] {
    std::string out = "cbmc " + versionOf(options_.cbmc);
    for (const char* f : kFlags) out += std::string(" ") + f;
    out += " " + std::to_string(options_.unwind);
    if (options_.strict) out += " --unsigned-overflow-check";
    if (options_.counterexample) out += " --trace";
    for (const std::string& f : solverFlags_) out += " " + f;
    if (!solverFlags_.empty()) out += "\nsolver " + versionOf(options_.solver);
    out += "\nmemory " + std::to_string(options_.memoryLimitMB) + "\n";  // tidsgränsen ingår inte
    for (const auto& c : options_.contracts) {
      out += "contract " + c + "\n";
      if (auto buf = llvm::MemoryBuffer::getFile(c)) out += (*buf)->getBuffer().str();
    }
    for (const auto& d : options_.includeDirs) {  // headrarna påverkar svaret
      out += "include " + d + "\n";
      if (auto buf = llvm::MemoryBuffer::getFile(d + "/pork_contracts.h")) out += (*buf)->getBuffer().str();
    }
    configuration_ = out;
  });
  return configuration_;
}

ProofResult CbmcProver::prove(const std::string& program) {
  auto cbmc = llvm::sys::findProgramByName(options_.cbmc);
  if (!cbmc) return std::unexpected(ProverError{"cbmc not found (" + options_.cbmc + ")"});

  TempFile source, output;
  if (auto r = process::writeProgram(source, program, options_.keepDir, count_); !r) return std::unexpected(r.error());
  if (auto r = process::makeTemp(output, "json"); !r) return std::unexpected(ProverError{r.error()});

  std::string unwind = std::to_string(options_.unwind);
  std::vector<llvm::StringRef> args{*cbmc, source.path};
  for (const auto& c : options_.contracts) args.push_back(c);
  for (const auto& d : options_.includeDirs) {
    args.push_back("-I");
    args.push_back(d);
  }
  for (const char* a : kFlags) args.push_back(a);
  args.push_back(unwind);
  if (options_.strict) args.push_back("--unsigned-overflow-check");
  if (options_.counterexample) args.push_back("--trace");
  for (const std::string& f : solverFlags_) args.push_back(f);

  std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(output.path),
                                                llvm::StringRef(output.path)};
  auto run = process::run("cbmc", *cbmc, args, redirects, {options_.timeoutSeconds, options_.memoryLimitMB});
  if (!run) return std::unexpected(run.error());
  int rc = *run;

  auto buf = llvm::MemoryBuffer::getFile(output.path);
  if (!buf) return std::unexpected(ProverError{"cannot read cbmc output"});
  auto json = llvm::json::parse((*buf)->getBuffer());
  if (!json) {
    std::string e = llvm::toString(json.takeError());
    if (options_.memoryLimitMB != 0 && rc != 0 && rc != 10)
      return std::unexpected(ProverError{"cbmc failed (exit " + std::to_string(rc) + "), probably out of memory (limit " +
                             std::to_string(options_.memoryLimitMB) + " MB, raise --silver-memory)"});
    return std::unexpected(ProverError{"cannot parse cbmc output (exit " + std::to_string(rc) + "): " + e});
  }
  const llvm::json::Array* messages = json->getAsArray();
  if (messages == nullptr) return std::unexpected(ProverError{"unexpected cbmc output"});

  std::vector<ProofFailure> failures;
  bool haveResult = false;
  std::string undecided;  // första egenskap som varken är SUCCESS eller FAILURE
  for (const llvm::json::Value& m : *messages) {
    const llvm::json::Object* o = m.getAsObject();
    if (o == nullptr) continue;
    if (str(*o, "messageType") == "ERROR")
      return std::unexpected(ProverError{"cbmc: " + str(*o, "messageText")});
    const llvm::json::Array* result = o->getArray("result");
    if (result == nullptr) continue;
    haveResult = true;
    for (const llvm::json::Value& p : *result) {
      const llvm::json::Object* prop = p.getAsObject();
      if (prop == nullptr) return std::unexpected(ProverError{"unexpected cbmc output"});
      std::string status = str(*prop, "status");
      if (status == "SUCCESS") continue;
      // Bara SUCCESS är bevisat. ERROR (lösaren misslyckades), UNKNOWN m.fl.
      // får aldrig räknas som bevis. cbmc lämnar egenskaper UNKNOWN när den
      // redan hittat ett motexempel; det är bara ett fel om inget FAILURE finns.
      if (status != "FAILURE") {
        if (undecided.empty()) undecided = "'" + str(*prop, "description") + "' (status " + status + ")";
        continue;
      }
      ProofFailure f;
      f.description = str(*prop, "description");
      if (const llvm::json::Object* loc = prop->getObject("sourceLocation")) {
        f.file = str(*loc, "file");
        unsigned line = 0;
        llvm::StringRef(str(*loc, "line")).getAsInteger(10, line);
        f.line = line;
      }
      llvm::StringRef d(f.description);
      if (d.consume_front("no body for callee ")) {
        f.missingFunction = d.str();
      } else if (d.contains("unwinding assertion")) {
        f.description = "loop may need more than " + unwind + " iterations (raise --unwind)";
      } else if (options_.counterexample) {
        f.modelInputs = modelInputs(*prop);
      }
      failures.push_back(std::move(f));
    }
  }
  if (!haveResult) return std::unexpected(ProverError{"cbmc gave no result (exit " + std::to_string(rc) + ")"});
  if (failures.empty() && !undecided.empty())
    return std::unexpected(ProverError{"cbmc could not decide " + undecided + "; solver error?"});
  return failures;
}

}  // namespace pork
