#include "ProverProcess.hpp"

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>

#include <chrono>
#include <thread>
#include <vector>

#include <signal.h>

namespace pork::process {

TempFile::~TempFile() {
  if (!path.empty() && !keep) llvm::sys::fs::remove(path);
}

std::expected<void, std::string> makeTemp(TempFile& t, const char* suffix) {
  if (auto ec = llvm::sys::fs::createTemporaryFile("pork-silver", suffix, t.path))
    return std::unexpected("cannot create temporary file: " + ec.message());
  return {};
}

std::expected<void, ProverError> writeProgram(TempFile& source, const std::string& program,
                                              const std::string& keepDir, std::atomic<unsigned>& counter) {
  if (!keepDir.empty()) {
    source.keep = true;
    llvm::sys::fs::create_directories(keepDir);
    source.path = keepDir;
    llvm::sys::path::append(source.path, "silver_" + std::to_string(++counter) + ".c");
  } else if (auto r = makeTemp(source, "c"); !r) {
    return std::unexpected(ProverError{r.error()});
  }
  std::error_code ec;
  llvm::raw_fd_ostream os(source.path, ec);
  if (ec) return std::unexpected(ProverError{"cannot write " + source.path.str().str() + ": " + ec.message()});
  os << program;
  return {};
}

std::string versionOf(const std::string& program) {
  auto path = llvm::sys::findProgramByName(program);
  if (!path) return "";
  llvm::SmallString<128> out;
  if (llvm::sys::fs::createTemporaryFile("pork-version", "txt", out)) return "";
  std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(out), llvm::StringRef(out)};
  llvm::sys::ExecuteAndWait(*path, {*path, "--version"}, std::nullopt, redirects);
  std::string v;
  if (auto buf = llvm::MemoryBuffer::getFile(out)) v = (*buf)->getBuffer().trim().split('\n').first.trim().str();
  llvm::sys::fs::remove(out);
  return v;
}

std::expected<int, ProverError> run(llvm::StringRef name, llvm::StringRef program,
                                    llvm::ArrayRef<llvm::StringRef> args,
                                    llvm::ArrayRef<std::optional<llvm::StringRef>> redirects, Limits limits) {
  static const llvm::ErrorOr<std::string> setsid = llvm::sys::findProgramByName("setsid");
  std::vector<llvm::StringRef> argv;
  llvm::StringRef exe = program;
  if (setsid) {
    exe = *setsid;
    argv.push_back(*setsid);
  }
  argv.insert(argv.end(), args.begin(), args.end());

  std::string err;
  bool failed = false;
  llvm::sys::ProcessInfo pi =
      llvm::sys::ExecuteNoWait(exe, argv, std::nullopt, redirects, limits.memoryLimitMB, &err, &failed);
  if (failed || pi.Pid == llvm::sys::ProcessInfo::InvalidPid)
    return std::unexpected(ProverError{"cannot run " + name.str() + ": " + err});
  // setsid execar bevisaren i samma process (den är inte gruppledare), så pid = grupp.
  auto killGroup = [&] {
    if (setsid) ::kill(-pi.Pid, SIGKILL);
  };
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limits.timeoutSeconds);
  while (true) {
    llvm::sys::ProcessInfo done = llvm::sys::Wait(pi, 0, &err);  // icke-blockerande
    if (done.Pid != 0) {
      killGroup();
      if (done.ReturnCode < 0) return std::unexpected(ProverError{name.str() + " crashed: " + err});
      return done.ReturnCode;
    }
    if (limits.timeoutSeconds != 0 && std::chrono::steady_clock::now() >= deadline) {
      killGroup();
      ::kill(pi.Pid, SIGKILL);
      llvm::sys::Wait(pi, std::nullopt, &err);
      return std::unexpected(ProverError{name.str() + " timed out after " + std::to_string(limits.timeoutSeconds) +
                                             " s (raise --silver-timeout)",
                                         true});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

}  // namespace pork::process
