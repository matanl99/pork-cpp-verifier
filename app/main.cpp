// Kompositionsrot: kopplar ihop clang-adaptern, kärnan och textrapporten.
//
//   pork a.cpp b.cpp -- -std=c++23
//   pork -p build/ src/*.cpp            (med compile_commands.json)
//   pork --trust trusted.txt ...        (betrodda funktioner, se TrustFile.hpp)
//   pork --silver ...                   (pröva Stone-funktioner för Silver med CBMC)
//   pork --silver --silver-prover=esbmc (samma med ESBMC, bitwuzla som standard)
//
// Exit: 0 ok, 1 pork::stone-krav brutet, 2 analysen kunde inte köras.

#include "Analyse.hpp"
#include "CachingProver.hpp"
#include "CbmcProver.hpp"
#include "EsbmcProver.hpp"
#include "ClangFactsSource.hpp"
#include "TextReport.hpp"
#include "TrustFile.hpp"

#include <clang/Tooling/CommonOptionsParser.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

#include <unistd.h>

namespace {
llvm::cl::OptionCategory category("pork options");
llvm::cl::opt<std::string> trustFile(
    "trust", llvm::cl::desc("File with trusted function names, one per line"),
    llvm::cl::value_desc("file"), llvm::cl::cat(category));
llvm::cl::opt<bool> silverOpt(
    "silver", llvm::cl::desc("Prove absence of run-time errors in Stone functions (needs cbmc)"),
    llvm::cl::cat(category));
llvm::cl::list<std::string> contractFiles(
    "contracts", llvm::cl::desc("C file with contracts for trusted functions (Silver)"),
    llvm::cl::value_desc("file.c"), llvm::cl::cat(category));
llvm::cl::opt<unsigned> unwind(
    "unwind", llvm::cl::desc("Loop iterations CBMC explores before giving up (default 64)"),
    llvm::cl::init(64), llvm::cl::cat(category));
llvm::cl::opt<unsigned> vectorBound(
    "vector-bound", llvm::cl::desc("Largest std::vector size Silver considers (default 16)"),
    llvm::cl::init(16), llvm::cl::cat(category));
llvm::cl::opt<unsigned> proverTimeout(
    "silver-timeout", llvm::cl::desc("Seconds per function for cbmc (default 120)"),
    llvm::cl::init(120), llvm::cl::cat(category));
llvm::cl::opt<unsigned> stoneJobs(
    "jobs", llvm::cl::desc("Translation units analysed in parallel (default: cores, at most RAM / 2 GB)"),
    llvm::cl::init(0), llvm::cl::cat(category));
llvm::cl::opt<unsigned> silverJobs(
    "silver-jobs",
    llvm::cl::desc("cbmc runs in parallel (default: cores, at most 3/4 of RAM / --silver-memory)"),
    llvm::cl::init(0), llvm::cl::cat(category));
llvm::cl::opt<unsigned> silverMemory(
    "silver-memory", llvm::cl::desc("Memory limit per cbmc run in MB (default 4096, 0 = none)"),
    llvm::cl::init(4096), llvm::cl::cat(category));
llvm::cl::opt<std::string> silverCache(
    "silver-cache", llvm::cl::desc("Directory for saved Silver results (default ~/.cache/pork/silver)"),
    llvm::cl::value_desc("dir"), llvm::cl::cat(category));
llvm::cl::opt<bool> noSilverCache(
    "no-silver-cache", llvm::cl::desc("Prove everything again, do not read or save results"),
    llvm::cl::cat(category));
llvm::cl::opt<bool> silverSlow(
    "silver-slow", llvm::cl::desc("Retry functions whose saved result is a timeout"),
    llvm::cl::cat(category));
llvm::cl::opt<std::string> silverProver(
    "silver-prover", llvm::cl::desc("Prover for Silver: cbmc or esbmc (default cbmc)"),
    llvm::cl::value_desc("name"), llvm::cl::init("cbmc"), llvm::cl::cat(category));
llvm::cl::opt<std::string> silverSolver(
    "silver-solver",
    llvm::cl::desc("Solver. cbmc: cadical (default if installed), minisat (built in, default otherwise), another "
                   "DIMACS SAT solver, or an SMT solver: z3, cvc5, bitwuzla. esbmc: bitwuzla (default), z3, ..."),
    llvm::cl::value_desc("name"), llvm::cl::cat(category));
llvm::cl::opt<bool> silverStrict(
    "silver-strict",
    llvm::cl::desc("Silver also rejects unsigned wraparound and integer conversions that lose the value"),
    llvm::cl::cat(category));
llvm::cl::opt<bool> silverCounterexample(
    "silver-counterexample", llvm::cl::desc("Show scalar model inputs for Silver findings (CBMC)"),
    llvm::cl::cat(category));
llvm::cl::opt<std::string> loansOpt(
    "loans",
    llvm::cl::desc("Loan model (always on, Stone needs both it and the rules): enforce, or report "
                   "(same verdict, plus a line with the model's and the rules' verdicts)"),
    llvm::cl::init("enforce"), llvm::cl::cat(category));
llvm::cl::opt<std::string> summariesFile(
    "summaries",
    llvm::cl::desc("Extra or overriding std summaries for the loan model (format: core/LoanSummaries.hpp)"),
    llvm::cl::value_desc("file"), llvm::cl::cat(category));
llvm::cl::list<std::string> onlyDirs(
    "only", llvm::cl::desc("Report (and prove) only functions in files under <dir>; repeatable"),
    llvm::cl::value_desc("dir"), llvm::cl::cat(category));
llvm::cl::opt<bool> flowOpt(
    "flow", llvm::cl::desc("Show the derived data flow of Stone functions (Bronze step 1, no verdict)"),
    llvm::cl::cat(category));
llvm::cl::opt<bool> flowOracleOpt(
    "flow-oracle",
    llvm::cl::desc("Try to refute --flow with two runs per output part (needs --flow --silver; a test oracle)"),
    llvm::cl::Hidden, llvm::cl::cat(category));
llvm::cl::opt<bool> flowOracleSelfTestOpt(
    "flow-oracle-selftest", llvm::cl::desc("Drop one dependency per output before the oracle (self-test)"),
    llvm::cl::Hidden, llvm::cl::cat(category));
llvm::cl::opt<std::string> keepC(
    "keep-c", llvm::cl::desc("Keep the generated C programs in this directory"),
    llvm::cl::value_desc("dir"), llvm::cl::cat(category));

// Stone: en TU per tråd. En TU med std-headers tar några hundra MB, så högst
// en per 2 GB RAM (oomd dödar annars terminalen, se --silver-memory).
unsigned analysisJobs() {
  if (stoneJobs != 0) return stoneJobs;
  unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
  long pages = ::sysconf(_SC_PHYS_PAGES), page = ::sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && page > 0) {
    unsigned long ramMB = static_cast<unsigned long>(pages) / (1024 * 1024 / static_cast<unsigned long>(page));
    jobs = static_cast<unsigned>(std::clamp<unsigned long>(ramMB / 2048, 1, jobs));
  }
  return jobs;
}
}

pork::OracleMode oracleMode() {
  if (!flowOracleOpt) return pork::OracleMode::off;
  return flowOracleSelfTestOpt ? pork::OracleMode::selfTest : pork::OracleMode::on;
}

int main(int argc, const char** argv) {
  llvm::cl::SetVersionPrinter([](llvm::raw_ostream& out) {
    out << "Pork " << PORK_VERSION << '\n';
  });
  auto parser = clang::tooling::CommonOptionsParser::create(argc, argv, category);
  if (!parser) {
    llvm::errs() << parser.takeError();
    return pork::analysisFailed;
  }

  if (silverCounterexample && (!silverOpt || silverProver != "cbmc")) {
    llvm::errs() << "pork: --silver-counterexample requires --silver and CBMC\n";
    return pork::analysisFailed;
  }

  pork::LoanOptions loans;
  if (loansOpt == "report") loans.mode = pork::LoanMode::report;
  else if (loansOpt == "enforce") loans.mode = pork::LoanMode::enforce;
  else {
    llvm::errs() << "pork: --loans must be enforce or report (the model is always on)\n";
    return pork::analysisFailed;
  }

  if (auto lib = pork::loadLoanSummaries(summariesFile); lib) {
    loans.summaries = std::move(*lib);
  } else {
    llvm::errs() << "pork: " << lib.error() << '\n';
    return pork::analysisFailed;
  }

  pork::TrustList trust;
  if (!trustFile.empty()) {
    auto loaded = pork::loadTrustList(trustFile);
    if (!loaded) {
      llvm::errs() << "pork: " << loaded.error() << '\n';
      return pork::analysisFailed;
    }
    trust = std::move(*loaded);
  }

  pork::ClangFactsSource source(parser->getCompilations(), parser->getSourcePathList(),
                                PORK_CLANG_RESOURCE_DIR, trust,
                                silverOpt ? std::optional(pork::TranslateOptions{vectorBound, silverStrict, silverCounterexample})
                                          : std::nullopt,
                                std::move(loans), analysisJobs(), flowOpt);
  std::vector<std::string> only;
  for (const std::string& d : onlyDirs) {
    llvm::SmallString<256> abs(d);
    if (!llvm::sys::fs::is_directory(abs)) {
      llvm::errs() << "pork: --only " << d << ": not a directory\n";
      return pork::analysisFailed;
    }
    llvm::sys::fs::make_absolute(abs);
    llvm::sys::path::remove_dots(abs, /*remove_dot_dot=*/true);
    only.emplace_back(abs);
  }
  pork::TextReport report(std::cout, std::cerr);
  if (!silverOpt) return pork::analyse(source, trust, report, nullptr, 1, only, pork::OracleMode::off, flowOpt);

  std::vector<std::string> contracts{PORK_CONTRACTS_DIR "/std.c"};
  contracts.insert(contracts.end(), contractFiles.begin(), contractFiles.end());
  std::unique_ptr<pork::IProver> proverPtr;
  if (silverProver == "cbmc") {
    pork::CbmcProver::Options options;
    options.contracts = contracts;
    options.includeDirs = {PORK_CONTRACTS_DIR};
    options.unwind = unwind;
    options.timeoutSeconds = proverTimeout;
    options.keepDir = keepC;
    options.memoryLimitMB = silverMemory;
    // cadical om den finns: på flyttalstunga program varierar minisats tid
    // kraftigt med formelns form (docs/reference.sv.md, "Lösare").
    std::string solver = silverSolver;
    if (solver.empty()) {
      solver = llvm::sys::findProgramByName("cadical") ? "cadical" : "minisat";
      if (solver == "minisat") llvm::errs() << "pork: silver: cadical not found, using minisat\n";
    }
    options.solver = solver;
    options.strict = silverStrict;
    options.counterexample = silverCounterexample;
    if (solver != "minisat" && !llvm::sys::findProgramByName(solver)) {
      llvm::errs() << "pork: solver '" << solver << "' not found\n";
      return pork::analysisFailed;
    }
    proverPtr = std::make_unique<pork::CbmcProver>(std::move(options));
  } else if (silverProver == "esbmc") {
    pork::EsbmcProver::Options options;
    options.contracts = contracts;
    options.includeDirs = {PORK_CONTRACTS_DIR};
    options.unwind = unwind;
    options.timeoutSeconds = proverTimeout;
    options.keepDir = keepC;
    options.memoryLimitMB = silverMemory;
    options.strict = silverStrict;
    if (silverSolver.getNumOccurrences() > 0) options.solver = silverSolver;  // annars bitwuzla
    if (!pork::EsbmcProver::knownSolver(options.solver)) {
      llvm::errs() << "pork: esbmc has no solver '" << options.solver << "'\n";
      return pork::analysisFailed;
    }
    proverPtr = std::make_unique<pork::EsbmcProver>(std::move(options));
  } else {
    llvm::errs() << "pork: unknown prover '" << silverProver << "' (cbmc or esbmc)\n";
    return pork::analysisFailed;
  }
  pork::IProver& prover = *proverPtr;
  unsigned jobs = silverJobs;
  if (jobs == 0) {
    jobs = std::max(1u, std::thread::hardware_concurrency());
    long pages = ::sysconf(_SC_PHYS_PAGES), page = ::sysconf(_SC_PAGE_SIZE);
    if (silverMemory != 0 && pages > 0 && page > 0) {
      unsigned long ramMB = static_cast<unsigned long>(pages) / (1024 * 1024 / static_cast<unsigned long>(page));
      jobs = static_cast<unsigned>(std::clamp<unsigned long>(ramMB * 3 / 4 / silverMemory, 1, jobs));
    }
  }
  if (noSilverCache || !keepC.empty()) {  // --keep-c: programmen ska faktiskt skrivas
    int rc = pork::analyse(source, trust, report, &prover, jobs, only, oracleMode(), flowOpt);
    return rc;
  }

  std::string dir = silverCache;
  if (dir.empty()) {
    llvm::SmallString<128> home;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0')
      home = xdg;
    else if (llvm::sys::path::home_directory(home))
      llvm::sys::path::append(home, ".cache");
    llvm::sys::path::append(home, "pork", "silver");
    dir = std::string(home);
  }
  pork::CachingProver cached(prover, {dir, silverSlow});
  int rc = pork::analyse(source, trust, report, &cached, jobs, only, oracleMode(), flowOpt);
  if (cached.hits() + cached.misses() > 0)
    llvm::errs() << "pork: silver: " << cached.hits() << " of " << cached.hits() + cached.misses()
                 << " from cache (" << dir << ")\n";
  if (cached.timeouts() > 0)
    llvm::errs() << "pork: silver: " << cached.timeouts() << " timed out at --silver-timeout=" << proverTimeout
                 << "; a run with a higher --silver-timeout retries only those\n";
  return rc;
}
