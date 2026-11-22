#pragma once

#include "Ports.hpp"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>

#include <atomic>
#include <expected>
#include <optional>
#include <string>

namespace pork::process {

// Gemensamt för bevisarna som körs som egna processer (cbmc, esbmc).

// Tar bort en temporärfil när den går ur räckvidd (om inte keep).
struct TempFile {
  llvm::SmallString<128> path;
  bool keep = false;
  TempFile() = default;
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  ~TempFile();
};

std::expected<void, std::string> makeTemp(TempFile& t, const char* suffix);

// Skriver programmet till en temporärfil, eller till keepDir/silver_<n>.c
// (--keep-c) där n räknas upp av counter.
std::expected<void, ProverError> writeProgram(TempFile& source, const std::string& program,
                                              const std::string& keepDir, std::atomic<unsigned>& counter);

// Första raden av `program --version`, eller "" om det inte går.
std::string versionOf(const std::string& program);

struct Limits {
  unsigned timeoutSeconds = 120;  // 0 = ingen
  unsigned memoryLimitMB = 4096;  // RLIMIT_AS per process, 0 = ingen
};

// Kör `program` med argv `args` (args[0] = programnamnet) och väntar med
// polling, så att flera trådar kan ha var sin process med var sin tidsgräns
// (inte SIGALRM, som delas av hela processen). Returnerar slutkoden.
//
// Processen startas via setsid(1) och blir ledare för en egen processgrupp.
// Lösare som bevisaren startar själv hamnar i samma grupp. När bevisaren har
// avslutats, hur det än gick (klar, timeout, slut på minne), dödas hela
// gruppen: annars kan en lösare bli föräldralös och fortsätta räkna.
//
// `name` används i felmeddelanden ("cbmc timed out after ...").
std::expected<int, ProverError> run(llvm::StringRef name, llvm::StringRef program,
                                    llvm::ArrayRef<llvm::StringRef> args,
                                    llvm::ArrayRef<std::optional<llvm::StringRef>> redirects, Limits limits);

}  // namespace pork::process
