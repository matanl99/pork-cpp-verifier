#include "EsbmcProver.hpp"

#include "ProverProcess.hpp"

#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <optional>
#include <set>

namespace pork {

namespace {

using process::TempFile;

// CBMC:s inbyggda funktioner i pork:s C-program och kontrakt, mappade till ESBMC.
constexpr const char* kCompat = R"(/* pork: CBMC builtins mapped to ESBMC */
#define __CPROVER_assert(c, msg) __ESBMC_assert((c), (msg))
#define __CPROVER_assume(c) __ESBMC_assume(c)
#define __CPROVER_isnanf(x) __builtin_isnan(x)
#define __CPROVER_isinff(x) __builtin_isinf(x)
#define __CPROVER_isnand(x) __builtin_isnan(x)
#define __CPROVER_isinfd(x) __builtin_isinf(x)
#define __CPROVER_isnanld(x) __builtin_isnan(x)
#define __CPROVER_isinfld(x) __builtin_isinf(x)
)";

// Flaggorna till esbmc (följt av antalet varv för --unwind).
constexpr const char* kFlags[] = {"--function",     "pork_harness",     "--overflow-check", "--nan-check",
                                  "--ub-shift-check", "--multi-property", "--quiet",          "--unwind"};

constexpr const char* kSolvers[] = {"bitwuzla", "z3", "boolector", "cvc5", "yices", "mathsat"};

// ESBMC:s namn på operationer -> pork:s (CBMC:s) ordalydelse.
std::optional<std::string> floatOp(llvm::StringRef op) {
  if (op == "ieee_add") return "addition";
  if (op == "ieee_sub") return "subtraction";
  if (op == "ieee_mul") return "multiplication";
  if (op == "ieee_div") return "division";
  return std::nullopt;
}

std::optional<std::string> signedOp(llvm::StringRef op) {
  if (op == "add") return "+";
  if (op == "sub") return "-";
  if (op == "mul") return "*";
  if (op == "div") return "/";
  if (op == "mod") return "%";
  if (op == "neg") return "unary minus";
  return std::nullopt;
}

}  // namespace

EsbmcProver::EsbmcProver(Options options) : options_(std::move(options)) {}

bool EsbmcProver::knownSolver(const std::string& solver) {
  for (const char* s : kSolvers)
    if (solver == s) return true;
  return false;
}

std::string EsbmcProver::normalize(llvm::StringRef d, unsigned unwind, bool strict) {
  if (d.starts_with("undefined behavior on shift operation") || d == "arithmetic overflow on shl")
    return "shift distance, operand or result out of range";
  llvm::StringRef rest = d;
  if (rest.consume_front("arithmetic overflow on floating-point ")) {
    if (auto op = floatOp(rest)) return "arithmetic overflow on floating-point " + *op;
  } else if (rest.consume_front("arithmetic overflow on ")) {
    if (auto op = signedOp(rest)) return std::string("arithmetic overflow on ") + (strict ? "integer " : "signed ") + *op;
  } else if (rest.consume_front("NaN on ")) {
    if (auto op = floatOp(rest)) return "NaN on " + *op;
  } else if (d == "dereference failure: NULL pointer") {
    return "dereference failure: pointer NULL";
  } else if (d.starts_with("unwinding assertion loop")) {
    return "loop may need more than " + std::to_string(unwind) + " iterations (raise --unwind)";
  }
  return d.str();
}

ProofResult EsbmcProver::parseOutput(llvm::StringRef output, int exitCode, unsigned unwind,
                                     unsigned memoryLimitMB, bool strict) {
  std::vector<ProofFailure> failures;
  std::set<std::string> missing;
  std::string undecided;  // första egenskap som varken är PASSED eller FAILED
  std::string errorLine;  // första felrad, för felmeddelandet
  bool successful = false, failed = false, inResults = false;
  std::string file;

  llvm::SmallVector<llvm::StringRef> lines;
  output.split(lines, '\n');
  for (llvm::StringRef raw : lines) {
    llvm::StringRef line = raw.rtrim();
    if (llvm::StringRef name = line; name.consume_front("WARNING: no body for function ")) {
      name = name.trim();
      if (missing.insert(name.str()).second)
        failures.push_back(ProofFailure{"no body for callee " + name.str(), "", 0, name.str()});
      continue;
    }
    if (line == "VERIFICATION SUCCESSFUL") successful = true;
    if (line == "VERIFICATION FAILED") failed = true;
    if (errorLine.empty() && (line.starts_with("ERROR:") || line.contains(" error: "))) errorLine = line.str();
    if (line == "** Results:") {
      inResults = true;
      continue;
    }
    if (!inResults) continue;
    if (line.empty()) {
      inResults = false;
      continue;
    }
    if (!line.starts_with(" ")) {  // "<fil>, function <namn>"
      file = line.rsplit(", function ").first.str();
      continue;
    }
    // "  FAILED       [id]  line 69  beskrivning"
    llvm::StringRef body = line.trim();
    auto [status, afterStatus] = body.split(" [");
    status = status.trim();
    auto [id, afterId] = afterStatus.split(']');
    (void)id;
    llvm::StringRef rest = afterId.trim();
    // Ibland följer villkoret: "... ieee_add [isinf(a.x) || ...]".
    if (rest.ends_with("]"))
      if (std::size_t open = rest.find(" ["); open != llvm::StringRef::npos) rest = rest.take_front(open);
    unsigned lineNo = 0;
    if (rest.consume_front("line")) {
      rest = rest.ltrim();
      auto [num, desc] = rest.split(' ');
      num.getAsInteger(10, lineNo);
      rest = desc.trim();
    }
    if (status == "PASSED") continue;
    if (status != "FAILED") {
      if (undecided.empty()) undecided = "'" + rest.str() + "' (status " + status.str() + ")";
      continue;
    }
    failures.push_back(ProofFailure{normalize(rest, unwind, strict), file, lineNo, ""});
  }

  // ESBMC listar ofta sina egna kontroller (arraygränser, överspill) före
  // pork:s kontrakt på samma rad. Kontraktet säger mer om orsaken och kommer
  // först hos CBMC; saknade kroppar ligger kvar allra först.
  std::stable_partition(failures.begin(), failures.end(), [](const ProofFailure& f) {
    llvm::StringRef d(f.description);
    return !f.missingFunction.empty() || d.starts_with("precondition of ") || d.starts_with("postcondition of ") ||
           d.starts_with("invariant of ") || d.starts_with("relies on ") || d.starts_with("pork: ");
  });

  if (successful && exitCode == 0) {
    if (!undecided.empty()) return std::unexpected(ProverError{"esbmc could not decide " + undecided});
    return failures;  // bara saknade kroppar, eller tomt = bevisat
  }
  if (failed) {
    if (failures.empty())
      return std::unexpected(ProverError{"esbmc reported VERIFICATION FAILED but no failed property"});
    return failures;
  }
  if (memoryLimitMB != 0 && exitCode != 0 && exitCode != 1 && errorLine.empty())
    return std::unexpected(ProverError{"esbmc failed (exit " + std::to_string(exitCode) +
                                       "), probably out of memory (limit " + std::to_string(memoryLimitMB) +
                                       " MB, raise --silver-memory)"});
  return std::unexpected(ProverError{"esbmc gave no result (exit " + std::to_string(exitCode) + ")" +
                                     (errorLine.empty() ? "" : ": " + errorLine)});
}

std::string EsbmcProver::configuration() const {
  std::call_once(configured_, [&] {
    std::string out = "esbmc " + process::versionOf(options_.esbmc);
    for (const char* f : kFlags) out += std::string(" ") + f;
    out += " " + std::to_string(options_.unwind) + " --" + options_.solver;
    if (options_.strict) out += " --unsigned-overflow-check";
    out += "\nmemory " + std::to_string(options_.memoryLimitMB) + "\n";  // tidsgränsen ingår inte
    out += kCompat;
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

ProofResult EsbmcProver::prove(const std::string& program) {
  auto esbmc = llvm::sys::findProgramByName(options_.esbmc);
  if (!esbmc) return std::unexpected(ProverError{"esbmc not found (" + options_.esbmc + ")"});

  TempFile source, compat, output;
  if (auto r = process::writeProgram(source, program, options_.keepDir, count_); !r) return std::unexpected(r.error());
  if (auto r = process::makeTemp(compat, "h"); !r) return std::unexpected(ProverError{r.error()});
  if (auto r = process::makeTemp(output, "txt"); !r) return std::unexpected(ProverError{r.error()});
  {
    std::error_code ec;
    llvm::raw_fd_ostream os(compat.path, ec);
    if (ec) return std::unexpected(ProverError{"cannot write " + compat.path.str().str() + ": " + ec.message()});
    os << kCompat;
  }

  std::string unwind = std::to_string(options_.unwind);
  std::string solver = "--" + options_.solver;
  std::vector<llvm::StringRef> args{*esbmc, "--include-file", compat.path, source.path};
  for (const auto& c : options_.contracts) args.push_back(c);
  for (const auto& d : options_.includeDirs) {
    args.push_back("-I");
    args.push_back(d);
  }
  for (const char* a : kFlags) args.push_back(a);
  args.push_back(unwind);
  args.push_back(solver);
  if (options_.strict) args.push_back("--unsigned-overflow-check");

  std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(output.path),
                                                llvm::StringRef(output.path)};
  auto run = process::run("esbmc", *esbmc, args, redirects, {options_.timeoutSeconds, options_.memoryLimitMB});
  if (!run) return std::unexpected(run.error());

  auto buf = llvm::MemoryBuffer::getFile(output.path);
  if (!buf) return std::unexpected(ProverError{"cannot read esbmc output"});
  return parseOutput((*buf)->getBuffer(), *run, options_.unwind, options_.memoryLimitMB, options_.strict);
}

}  // namespace pork
