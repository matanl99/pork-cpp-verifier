// pork-args: --silver --vector-bound 8
//
// - range-for över en std::string (en sekvens, som std::vector).
// - static const-medlemmar med konstant initierare (std::string::npos) är
//   kompileringstidskonstanter, som constexpr.
// - Egna C-kontrakt (44_text_and_contracts.contracts.c) får samma hjälpmedel
//   som contracts/std.c via #include "pork_contracts.h".

#include <cmath>
#include <string>
#include <pork/annotations.hpp>

// rules: STN-35, SLV-35
// expect: silver: assuming
int count_digits(const std::string& s) { int n = 0; for (char c : s) { if (c >= '0' && c <= '9') n = n + 1; } return n; }

// rules: STN-35, SLV-35
// expect: silver: assuming
void upcase(std::string& s) { for (char& c : s) { if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); } }

// ODR-use får libstdc++ att instansiera npos-definitionen utanför klassen.
const auto* const npos_address = &std::string::npos;

// rules: STN-18
// expect: silver: assuming
bool not_npos(const std::string& s) { return s.size() != std::string::npos; }

struct Limits { static const int max_len = 5; };

// rules: STN-18
// expect: silver
int one_past() { return Limits::max_len + 1; }

int calls = 0;

// rules: STN-22
// expect: not-stone: uses variable 'calls' with static storage
int read_calls() { return calls; }

// glibc deklarerar round globalt (std::round är ::round): trust-namnet är "round".
// rules: SLV-21
// expect: silver: assuming round
double cents(double v) { PORK_PRE(v >= -1e9 && v <= 1e9); return std::round(v * 100.0) / 100.0; }
