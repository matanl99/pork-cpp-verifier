#include "CachingProver.hpp"

#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/raw_ostream.h>

#include <chrono>
#include <optional>

namespace pork {

namespace {

// Höj om filformatet eller tolkningen ändras: gamla filer läses då inte.
// 2: status ERROR räknades som bevisat. 3: tidsgränsen ur nyckeln, sparas med timeouten.
constexpr int kFormat = 3;

struct Entry {
  ProofResult result;
  double seconds = 0;
  unsigned timeoutLimit = 0;  // för en timeout: gränsen den slog i
};

llvm::json::Value toJson(const ProofResult& r, double seconds, unsigned timeoutLimit) {
  llvm::json::Object o{{"format", kFormat}, {"seconds", seconds}};
  if (r) {
    llvm::json::Array failures;
    for (const ProofFailure& f : *r)
      failures.push_back(llvm::json::Object{{"description", f.description},
                                            {"file", f.file},
                                            {"line", static_cast<int64_t>(f.line)},
                                            {"missingFunction", f.missingFunction},
                                            {"modelInputs", f.modelInputs}});
    o["failures"] = std::move(failures);
  } else {
    o["timeout"] = r.error().message;
    o["timeoutLimit"] = static_cast<int64_t>(timeoutLimit);
  }
  return o;
}

std::optional<Entry> fromJson(llvm::StringRef text) {
  auto v = llvm::json::parse(text);
  if (!v) {
    llvm::consumeError(v.takeError());
    return std::nullopt;
  }
  const llvm::json::Object* o = v->getAsObject();
  if (o == nullptr || o->getInteger("format") != kFormat) return std::nullopt;
  Entry e;
  e.seconds = o->getNumber("seconds").value_or(0);
  if (auto t = o->getString("timeout")) {
    e.result = std::unexpected(ProverError{t->str(), true});
    e.timeoutLimit = static_cast<unsigned>(o->getInteger("timeoutLimit").value_or(0));
    return e;
  }
  const llvm::json::Array* failures = o->getArray("failures");
  if (failures == nullptr) return std::nullopt;
  std::vector<ProofFailure> out;
  for (const llvm::json::Value& fv : *failures) {
    const llvm::json::Object* f = fv.getAsObject();
    if (f == nullptr) return std::nullopt;
    ProofFailure pf;
    pf.description = f->getString("description").value_or("").str();
    pf.file = f->getString("file").value_or("").str();
    pf.line = static_cast<unsigned>(f->getInteger("line").value_or(0));
    pf.missingFunction = f->getString("missingFunction").value_or("").str();
    pf.modelInputs = f->getString("modelInputs").value_or("").str();
    out.push_back(std::move(pf));
  }
  e.result = std::move(out);
  return e;
}

// Täcker en sparad timeout (gränsen `saved`) en körning med gränsen `current`?
// 0 = ingen gräns. Bara om den nya gränsen inte är större.
bool timeoutStillApplies(unsigned saved, unsigned current) {
  return saved != 0 && current != 0 && current <= saved;
}

}  // namespace

std::string CachingProver::key(const std::string& program) const {
  llvm::SHA256 sha;
  std::string config = inner_.configuration();
  sha.update(config);
  sha.update(llvm::StringRef("\0", 1));  // skiljetecken: konfiguration | program
  sha.update(program);
  std::array<uint8_t, 32> digest = sha.final();
  return llvm::toHex(digest, /*LowerCase=*/true);
}

ProofResult CachingProver::prove(const std::string& program) {
  llvm::SmallString<256> path(options_.dir);
  std::string k = key(program);
  llvm::sys::path::append(path, k.substr(0, 2), k + ".json");

  if (auto buf = llvm::MemoryBuffer::getFile(path)) {
    auto e = fromJson((*buf)->getBuffer());
    bool reuse = e && (e->result || (!options_.retryTimeouts &&
                                     timeoutStillApplies(e->timeoutLimit, inner_.timeoutSeconds())));
    if (reuse) {
      ++hits_;
      if (!e->result) {
        ++timeouts_;
        return std::unexpected(ProverError{e->result.error().message + " (cached)", true});
      }
      return e->result;
    }
  }

  ++misses_;
  auto start = std::chrono::steady_clock::now();
  ProofResult r = inner_.prove(program);
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (!r && !r.error().timedOut) return r;  // körningsfel sparas inte
  if (!r) ++timeouts_;

  // Skriv till en temporärfil i samma katalog och flytta den på plats.
  // Misslyckas det är svaret ändå rätt; det sparas bara inte.
  if (llvm::sys::fs::create_directories(llvm::sys::path::parent_path(path))) return r;
  llvm::SmallString<256> temp;
  int fd = -1;
  if (llvm::sys::fs::createUniqueFile(path + ".tmp-%%%%%%%%", fd, temp)) return r;
  {
    llvm::raw_fd_ostream os(fd, /*shouldClose=*/true);
    os << toJson(r, seconds, inner_.timeoutSeconds());
  }
  if (llvm::sys::fs::rename(temp, path)) (void)llvm::sys::fs::remove(temp);
  return r;
}

}  // namespace pork
