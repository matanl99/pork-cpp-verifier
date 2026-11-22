#include "TrustFile.hpp"

#include <fstream>

namespace pork {

TrustList parseTrustList(std::istream& in) {
  TrustList list;
  std::string line;
  while (std::getline(in, line)) {
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos) continue;
    auto last = line.find_last_not_of(" \t\r");
    std::string entry = line.substr(first, last - first + 1);
    if (entry.starts_with("type ")) {
      auto name = entry.find_first_not_of(" \t", 5);
      if (name != std::string::npos) list.types.insert(entry.substr(name));
    } else {
      list.names.insert(entry);
    }
  }
  return list;
}

std::expected<TrustList, std::string> loadTrustList(const std::string& path) {
  std::ifstream in(path);
  if (!in) return std::unexpected("cannot read trust file '" + path + "'");
  return parseTrustList(in);
}

}  // namespace pork
