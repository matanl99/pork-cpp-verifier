#include "UnionCheckProver.hpp"

#include <cctype>

namespace pork {

// Nyckelordet union i koden: förprocessorrader (#line "…/union/…"), strängar,
// teckenkonstanter och kommentarer hoppas över.
bool UnionCheckProver::hasUnion(const std::string& program) {
  auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
  const std::size_t n = program.size();
  bool lineStart = true;
  for (std::size_t i = 0; i < n;) {
    char c = program[i];
    if (c == '\n') {
      lineStart = true;
      ++i;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
      continue;
    }
    if (lineStart && c == '#') {  // förprocessorrad, med fortsättningsrader
      while (i < n && program[i] != '\n') i += program[i] == '\\' && i + 1 < n ? 2 : 1;
      continue;
    }
    lineStart = false;
    if (c == '/' && i + 1 < n && program[i + 1] == '/') {
      while (i < n && program[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && program[i + 1] == '*') {
      std::size_t end = program.find("*/", i + 2);
      i = end == std::string::npos ? n : end + 2;
      continue;
    }
    if (c == '"' || c == '\'') {
      for (++i; i < n && program[i] != c && program[i] != '\n'; ++i)
        if (program[i] == '\\') ++i;
      ++i;
      continue;
    }
    if (word(c)) {
      std::size_t j = i;
      while (j < n && word(program[j])) ++j;
      if (program.compare(i, j - i, "union") == 0) return true;
      i = j;
      continue;
    }
    ++i;
  }
  return false;
}

ProofResult UnionCheckProver::prove(const std::string& program) {
  ProofResult r = main_.prove(program);
  if (!r || r->empty() || !hasUnion(program)) return r;
  ++checked_;
  ProofResult c = check_.prove(program);
  if (c && c->empty()) {
    ++overturned_;
    return c;
  }
  if (!c) {
    const ProofFailure& f = r->front();
    std::string found = f.description + (f.line != 0 ? " at " + f.file + ":" + std::to_string(f.line) : "");
    return std::unexpected(ProverError{"cbmc found " + found + " in a program with a union, and the check with " +
                                           checkName_ + " did not finish: " + c.error().message,
                                       c.error().timedOut});
  }
  return r;
}

std::string UnionCheckProver::configuration() const {
  return main_.configuration() + "\nunion check:\n" + check_.configuration();
}

}  // namespace pork
