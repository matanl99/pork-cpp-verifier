// pork-args: --silver --vector-bound 4
//
// std::move(x) (STN-57): efter en move är x giltigt men ospecificerat. x ska
// vara (en väg i) en lokal variabel eller värdeparameter, och får inte
// användas efteråt (clangs LiveVariables; en senare tilldelning räknas också
// som användning, försiktigt) eller i samma anrop (ospecificerad ordning).
// Silver: en kopia.

#include <string>
#include <utility>
#include <vector>

struct Log { std::vector<std::string> lines; };

// rules: STN-57, SLV-13
// expect: stone: not silver: std::vector may grow beyond 4 elements
void add(Log& log, std::string line) { log.lines.push_back(std::move(line)); }

// rules: STN-57
// expect: silver: assuming
std::vector<std::string> take(Log log) { return std::move(log.lines); }

// rules: STN-57
// expect: silver: assuming
int moved_len(std::string s) { std::string t = std::move(s); return int(t.size()); }

// rules: STN-57
// expect: not-stone: 's' is used after std::move(s)
int used_after(std::string s) { std::string t = std::move(s); return int(s.size() + t.size()); }

// rules: STN-57
// expect: not-stone: 'line' is used after std::move(line)
void reassigned(Log& log, std::string line) { log.lines.push_back(std::move(line)); line = "x"; }

// rules: STN-57
// expect: not-stone: 'line' is used after std::move(line)
void in_loop(Log& log, std::string line, int n) { for (int i = 0; i < n; ++i) log.lines.push_back(std::move(line)); }

// rules: STN-57
// expect: not-stone: std::move of something other than a local variable or value parameter
void from_ref(Log& log, std::string& line) { log.lines.push_back(std::move(line)); }

// rules: STN-12
// expect: silver: assuming
int both(std::string a, const std::string& b) { return int(a.size() + b.size()); }

// rules: STN-57
// (Fångas här redan av livsanalysen: clang lägger s efter flytten.)
// expect: not-stone: 's' is used
int same_call(std::string s) { return both(std::move(s), s); }
