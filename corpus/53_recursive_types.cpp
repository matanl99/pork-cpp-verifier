// pork-args: --silver
#include <optional>
#include <vector>

// Rekursiva typer (STN-63). Hittat när yaml-cpp mättes: pork
// kraschade av oändlig rekursion i typreglerna. En typ som innehåller sig
// själv via betrodda typer är en värdetyp om den är det under antagandet att
// den är det (koinduktivt). Silver översätter dem inte än.

// Typen innehåller sig själv via en betrodd vektor.
struct Node {
  int v;
  std::vector<Node> kids;
};

// rules: STN-63
// expect: stone: not silver: cannot translate to C: recursive type Node (not translated yet)
int value(const Node& n) { return n.v; }

// rules: STN-63
// expect: stone: not silver
int count(const Node& n) { return static_cast<int>(n.kids.size()); }

// Ömsesidigt rekursiva typer.
struct B;
struct A {
  std::vector<B> bs;
};
struct B {
  std::vector<A> as;
};

// rules: STN-63
// expect: stone: not silver
int size(const A& a) { return static_cast<int>(a.bs.size()); }

// Rekursion hjälper inte en typ som inte är en värdetyp av annat skäl.
struct WithPointer {
  int* p;
  std::vector<WithPointer> more;
};

// rules: STN-63, STN-3
// expect: not-stone: parameter 'w' has type const WithPointer & (field 'p' of WithPointer has type int *)
int first(const WithPointer& w) { return static_cast<int>(w.more.size()); }

// Samma typ två gånger utan rekursion är ingen rekursion.
struct Pair {
  std::optional<int> x;
  std::optional<int> y;
};

// rules: STN-4
// expect: silver: assuming
int first(const Pair& p) { return p.x.value_or(0); }
