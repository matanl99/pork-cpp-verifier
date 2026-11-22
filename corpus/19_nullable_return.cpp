// pork-args: --silver
//
// Returtyp `const T*` (T värdetyp): en pekare som kan vara null in i *this
// (const metod) eller en const&-parameter. Varje return är `nullptr`, ett
// get_if-lån eller `std::get_if<T>(&v)` på en sådan väg, eller anropet av en
// annan sådan funktion. Hos anroparen är `const T* p = f(...)` ett lån som
// kan vara null, som get_if: p får testas, jämföras med nullptr och
// derefereras, och lånet gäller objektet/det enda referensargumentet.
// Bara egna Stone-funktioner (en betrodd funktion kan peka vart som helst).
//
// Silver: C-funktionen returnerar `{_Bool has; T v;}` (en const&-parameter
// är en kopia i C, så en C-pekare in i den skulle hänga); deref kontrolleras.

#include <pork/annotations.hpp>

#include <variant>
#include <vector>

struct Run { int who{0}; };
struct Pass { int from{0}; int to{0}; };
using Cmd = std::variant<Run, Pass>;
struct Planned { int tick{0}; Cmd cmd; };

// --- Definitioner ---

// rules: STN-11, SLV-27
// expect: silver: assuming
const Pass* as_pass(const Planned& p) { return std::get_if<Pass>(&p.cmd); }

// rules: STN-11, SLV-27
// expect: silver: assuming
const Pass* pass_from(const std::vector<Planned>& planned, int who) {
  for (const Planned& p : planned)
    if (const auto* pass = std::get_if<Pass>(&p.cmd); pass != nullptr && pass->from == who) return pass;
  return nullptr;
}

// Vidarebefordran: samma lån.
// rules: STN-11, SLV-27
// expect: silver: assuming
const Pass* first_pass(const std::vector<Planned>& planned) { return pass_from(planned, 0); }

struct Turn {
  Planned current;
  // rules: STN-11, SLV-27
  // expect: silver: assuming
  const Pass* pass() const { return std::get_if<Pass>(&current.cmd); }
};

// --- Anroparen ---

// rules: STN-39, SLV-27
// expect: silver: assuming
int holder(const std::vector<Planned>& planned, int carrier) {
  const Pass* pass = pass_from(planned, carrier);
  return pass == nullptr ? carrier : pass->to;
}

// rules: STN-39, SLV-27
// expect: silver: assuming
int receiver(const Turn& t) {
  if (const Pass* p = t.pass()) return p->to;
  return -1;
}

// Beviset följer värdet: passningen från 3 går till 7, annars bäraren.
// rules: STN-39, SLV-27, SLV-18
// expect: silver: assuming
int known() { PORK_POST(int r, r == 7); std::vector<Planned> v{}; v.push_back(Planned{0, Pass{3, 7}}); return holder(v, 3); }
// rules: STN-39, SLV-27, SLV-18
// expect: stone: not silver: postcondition of 'wrong'
int wrong() { PORK_POST(int r, r == 8); std::vector<Planned> v{}; v.push_back(Planned{0, Pass{3, 7}}); return holder(v, 3); }
// rules: STN-39, SLV-27, SLV-18
// expect: silver: assuming
int missing() { PORK_POST(int r, r == 5); std::vector<Planned> v{}; v.push_back(Planned{0, Pass{3, 7}}); return holder(v, 5); }

// Deref utan test: null är möjligt.
// rules: STN-39, SLV-12
// expect: stone: not silver: dereference failure: pointer NULL
int unchecked(const Planned& p) { const Pass* pass = as_pass(p); return pass->to; }

// Lånet gäller medan pekaren används.
// rules: STN-39, STN-62
// expect: not-stone: loans: changes 'planned' while 'pass' borrows 'planned[*].cmd[*]'
int modify_while_borrowed(std::vector<Planned>& planned) {
  const Pass* pass = pass_from(planned, 1);
  planned.clear();
  return pass == nullptr ? 0 : pass->to;
}

// Pekaren får flyttas till ett annat objekt (lånemodellen följer det nya
// lånet). Silver översätter inte tilldelning av pekare än.
// rules: STN-39, STN-62, SLV-2
// expect: stone: not silver: cannot translate to C: assignment to a pointer
int reseat(const Planned& a, const Planned& b) {
  const Pass* pass = as_pass(a);
  pass = as_pass(b);
  return pass == nullptr ? 0 : pass->to;
}

// Temporär som argument: pekaren skulle hänga.
// rules: STN-39, STN-36
// expect: not-stone: loans: 'pass' borrows a temporary
int through_temporary() { const Pass* pass = as_pass(Planned{}); return pass == nullptr ? 0 : pass->to; }

// --- Definitioner som inte är Stone ---

// rules: STN-11
// expect: not-stone: loans: returns a loan of 'p.cmd[*]', which does not outlive the function
const Pass* from_value(Planned p) { return std::get_if<Pass>(&p.cmd); }

// Range-for med kopia: pekaren skulle peka in i kopian.
// rules: STN-11
// expect: not-stone: loans: returns a loan of 'p.cmd[*]', which does not outlive the function
const Pass* from_copy(const std::vector<Planned>& planned) {
  for (Planned p : planned)
    if (const auto* pass = std::get_if<Pass>(&p.cmd)) return pass;
  return nullptr;
}

// Lokal kopia.
// rules: STN-11
// expect: not-stone: loans: returns a loan of 'local.cmd[*]', which does not outlive the function
const Pass* from_local(const Planned& p) { Planned local = p; return std::get_if<Pass>(&local.cmd); }

// rules: STN-11
// expect: not-stone: return type Pass *
Pass* mutable_pointer(Planned& p) { return std::get_if<Pass>(&p.cmd); }

struct Holder {
  Planned current;
  // rules: STN-11
  // expect: not-stone: return type const Pass * needs a const method
  const Pass* pass() { return std::get_if<Pass>(&current.cmd); }
};
