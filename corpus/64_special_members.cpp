// Egna kopierings- och flyttfunktioner (STN-72), return *this (STN-73).
// Typerna är värdetyper; specialmedlemmarna är funktioner som granskas, och
// varje implicit kopia eller flytt som kör dem är ett anrop av dem. Fallen
// kommer från en egen, opublicerad samling stilexempel (002-010, 023, 036,
// 037, 040) och en genomgång av var en kopia kan köras utan att synas.
#include <optional>
#include <utility>
#include <vector>

int seen = 0;

// --- Korrekta specialmedlemmar ------------------------------------------------

struct Label {
  std::vector<int> text;
  int copies = 0;
  // rules: STN-72, STN-66
  // expect: stone: assuming
  Label(std::vector<int> t) : text(std::move(t)) {}
  // rules: STN-72
  // expect: stone: assuming
  Label(const Label& o) : text(o.text), copies(o.copies + 1) {}
  // rules: STN-72, STN-57
  // expect: stone: assuming
  Label(Label&& o) noexcept : text(std::move(o.text)), copies(o.copies) { o.copies = 0; }
  // rules: STN-72, STN-73
  // expect: stone: assuming
  Label& operator=(const Label& o) {
    text = o.text;
    copies = o.copies + 1;
    return *this;
  }
  // rules: STN-72, STN-73
  // expect: stone: assuming
  Label& operator=(Label&& o) noexcept {
    text = std::move(o.text);
    copies = std::exchange(o.copies, 0);
    return *this;
  }
};

// rules: STN-72
// expect: stone: assuming
int copy_label() {
  Label a{{1, 2}};
  Label b = a;
  return b.copies;
}

// rules: STN-72, STN-57
// expect: stone: assuming
int move_label() {
  Label a{{1, 2}};
  Label b = std::move(a);
  return static_cast<int>(b.text.size());
}

// rules: STN-72
// expect: stone: assuming
int assign_label() {
  Label a{{1}};
  Label b{{2, 3}};
  b = a;
  return static_cast<int>(b.text.size());
}

// Copy-and-swap: parametern som värde, sedan byte.
struct Buffer {
  std::vector<int> data;
  // rules: STN-72, STN-73
  // expect: stone: assuming
  Buffer& operator=(Buffer other) {
    data.swap(other.data);
    return *this;
  }
};

// rules: STN-72
// expect: stone: assuming
int copy_and_swap() {
  Buffer a{{4, 8}};
  Buffer b{{2}};
  b = a;
  return b.data.back();
}

// Fält för fält ur källan: varje fält flyttas en gång.
struct Pair {
  std::vector<int> a;
  std::vector<int> b;
  // rules: STN-72, STN-57
  // expect: stone: assuming
  Pair(Pair&& o) noexcept : a(std::move(o.a)), b(std::move(o.b)) {}
};

// Initierarna körs i deklarationsordning: n läser o.a innan a flyttar det.
struct ReadsFirst {
  int n;
  std::vector<int> a;
  // rules: STN-72, STN-57
  // expect: stone: assuming
  ReadsFirst(ReadsFirst&& o) noexcept : n(static_cast<int>(o.a.size())), a(std::move(o.a)) {}
};

// --- Felaktiga specialmedlemmar ------------------------------------------------

struct MovedTwice {
  std::vector<int> a;
  std::vector<int> b;
  // rules: STN-72, STN-57
  // expect: not-stone: 'o.a' is used after std::move(o.a)
  MovedTwice(MovedTwice&& o) noexcept : a(std::move(o.a)), b(std::move(o.a)) {}
};

// Skriven först men körs efter: a deklareras före n.
struct DeclarationOrder {
  std::vector<int> a;
  int n;
  // rules: STN-72, STN-57
  // expect: not-stone: 'o.a' is used after std::move(o.a)
  DeclarationOrder(DeclarationOrder&& o) noexcept : n(static_cast<int>(o.a.size())), a(std::move(o.a)) {}
};

struct ReadsInBody {
  std::vector<int> a;
  int n = 0;
  // rules: STN-72, STN-57
  // expect: not-stone: 'o.a' is used after std::move(o.a)
  ReadsInBody(ReadsInBody&& o) noexcept : a(std::move(o.a)) { n = static_cast<int>(o.a.size()); }
};

struct CopiesWholeSource {
  std::vector<int> a;
  int n = 0;
  // rules: STN-72
  // expect: stone: assuming
  CopiesWholeSource(const CopiesWholeSource&) = default;
  // rules: STN-72, STN-57
  // expect: not-stone: 'o' is used after std::move(o.a)
  CopiesWholeSource(CopiesWholeSource&& o) noexcept : a(std::move(o.a)) {
    CopiesWholeSource c = o;
    n = c.n;
  }
};

struct ReturnsOther {
  int n = 0;
  // rules: STN-73
  // expect: not-stone: a method returning its own class by reference may only return *this
  ReturnsOther& pick(ReturnsOther& o) { return o; }
};

// T&& är bara tillåten som källa i flyttfunktioner.
// rules: STN-12, STN-72
// expect: not-stone: parameter 'v' has type std::vector<int> &&
int takes_rvalue(std::vector<int>&& v) { return static_cast<int>(v.size()); }

// rules: STN-72, STN-57
// expect: not-stone: 'a' is used in the same call as std::move(a)
int self_move() {
  Label a{{1}};
  a = std::move(a);
  return a.copies;
}

// Ett flyttat objekt läses inte (STN-57 gäller som förut hos anroparen).
// rules: STN-72, STN-57
// expect: not-stone: 'a' is used after std::move(a)
int read_after_move() {
  Label a{{1}};
  Label b = std::move(a);
  return a.copies + b.copies;
}

// --- Förorenade kopior: varje väg en kopia kan köras ----------------------------

struct Dirty {
  int v = 0;
  // rules: STN-72, STN-66
  // expect: stone
  Dirty() = default;
  // rules: STN-72, STN-66
  // expect: stone
  Dirty(int x) : v(x) {}
  // rules: STN-72, STN-22
  // expect: not-stone: uses variable 'seen' with static storage
  Dirty(const Dirty& o) : v(o.v) { ++seen; }
};

struct HoldsDirty {
  Dirty d;
  int w = 0;
};

// rules: STN-72, STN-23
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_copy_init() {
  Dirty a{1};
  Dirty b = a;
  return b.v;
}

// rules: STN-72, STN-23
// expect: stone
int take_dirty(Dirty d) { return d.v; }

// rules: STN-72, STN-23
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_by_value() {
  Dirty a{1};
  return take_dirty(a);
}

// Den implicita kopieringen av den yttre typen kopierar fältet.
// rules: STN-72, STN-67
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_enclosing_copy() {
  HoldsDirty o{};
  HoldsDirty p = o;
  return p.w;
}

// rules: STN-72, STN-35
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_range_for(const std::vector<Dirty>& v) {
  int s = 0;
  for (Dirty d : v) s = s + d.v;
  return s;
}

// rules: STN-72, STN-35
// expect: stone: assuming
int clean_range_for_reference(const std::vector<Dirty>& v) {
  int s = 0;
  for (const Dirty& d : v) s = s + d.v;
  return s;
}

// rules: STN-72, STN-40
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_lambda_capture() {
  Dirty a{1};
  auto f = [a]() { return a.v; };
  return f();
}

// rules: STN-72, STN-67
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_vector_copy(const std::vector<Dirty>& v) {
  std::vector<Dirty> w = v;
  return static_cast<int>(w.size());
}

// std::optional kopierar sitt värde genom en ärvd konstruktor
// (_Optional_payload ärver _Optional_payload_base).
// rules: STN-72, STN-67
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_optional_copy(const std::optional<Dirty>& o) {
  std::optional<Dirty> p = o;
  return p ? 1 : 0;
}

// rules: STN-72, STN-67
// expect: not-stone: calls non-stone 'Dirty::Dirty'
int dirty_optional_value_or(const std::optional<Dirty>& o) { return o.value_or(Dirty{2}).v; }

// Garanterad elision: ingen kopia körs.
// rules: STN-72
// expect: stone
int dirty_prvalue() {
  Dirty b = Dirty{3};
  return b.v;
}

struct DirtyMove {
  int v = 0;
  // rules: STN-72, STN-66
  // expect: stone
  DirtyMove() = default;
  DirtyMove(const DirtyMove&) = default;  // används inte: ingen kropp, ingen dom
  // rules: STN-72, STN-22
  // expect: not-stone: uses variable 'seen' with static storage
  DirtyMove(DirtyMove&& o) noexcept : v(o.v) { ++seen; }
  DirtyMove& operator=(const DirtyMove&) = default;  // används inte
  // rules: STN-72
  // expect: stone
  DirtyMove& operator=(DirtyMove&&) = default;
};

// rules: STN-72, STN-23
// expect: not-stone: calls non-stone 'DirtyMove::DirtyMove'
DirtyMove dirty_return_local() {
  DirtyMove m{};
  return m;
}

// rules: STN-72, STN-67
// expect: not-stone: calls non-stone 'DirtyMove::DirtyMove'
int dirty_swap() {
  DirtyMove a{};
  DirtyMove b{};
  std::swap(a, b);
  return a.v;
}

struct DirtyAssign {
  int v = 0;
  // rules: STN-72, STN-73, STN-22
  // expect: not-stone: uses variable 'seen' with static storage
  DirtyAssign& operator=(const DirtyAssign& o) {
    v = o.v + seen;
    return *this;
  }
};

struct HoldsDirtyAssign {
  DirtyAssign a;
  int w = 0;
};

// rules: STN-72, STN-23
// expect: not-stone: calls non-stone 'DirtyAssign::operator='
int dirty_assign() {
  DirtyAssign a{};
  DirtyAssign b{};
  b = a;
  return b.v;
}

// rules: STN-72, STN-67
// expect: not-stone: calls non-stone 'DirtyAssign::operator='
int dirty_enclosing_assign() {
  HoldsDirtyAssign a{};
  HoldsDirtyAssign b{};
  b = a;
  return b.w;
}

// --- return *this hos anroparen -------------------------------------------------

struct Counter {
  int n = 0;
  // rules: STN-73
  // expect: stone
  Counter& operator+=(const Counter& o) {
    n = n + o.n;
    return *this;
  }
};

// rules: STN-73
// expect: stone
int add_counters() {
  Counter a{1};
  Counter b{2};
  a += b;
  return a.n;
}

// Resultatet är objektet: r är en referens till a.
// rules: STN-73, STN-31
// expect: stone
int bind_result() {
  Counter a{1};
  Counter b{2};
  Counter& r = (a += b);
  b.n = 3;
  return r.n + b.n;
}

// rules: STN-73, STN-62
// expect: not-stone: may be the same object
int add_to_itself(Counter& a) {
  a += a;
  return a.n;
}

// rules: STN-73, STN-20
// expect: not-stone: side effect inside expression
int result_in_expression() {
  Counter a{1};
  Counter b{2};
  return (a += b).n;
}

// rules: STN-73, STN-20
// expect: not-stone: side effect inside expression
int chained() {
  Counter a{};
  Counter b{};
  Counter c{};
  a += b += c;
  return a.n;
}
