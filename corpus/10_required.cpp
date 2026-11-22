// Krav (opt-in, som SPARK_Mode): [[clang::annotate("pork::stone")]] betyder
// att funktionen MÅSTE vara Stone. Annars error på stderr och exit 1.
// `// expect-violation` (ev. med namn) anger att ett sådant fel förväntas.

int g = 0;

// rules: STN-22
// expect: not-stone: uses variable 'g'
int impure(int a) { return a + g; }

// rules: STN-45
// expect: stone
[[clang::annotate("pork::stone")]] int required_ok(int a) { return a + 1; }

// rules: STN-45
// expect: not-stone: uses variable 'g'
// expect-violation
[[clang::annotate("pork::stone")]] int required_bad(int a) { return a + g; }

// rules: STN-45, STN-23
// expect: not-stone: calls non-stone 'impure'
// expect-violation
[[clang::annotate("pork::stone")]] int required_transitive(int a) { return impure(a); }

// Annoteringen på en deklaration (som i en header) gäller definitionen.
[[clang::annotate("pork::stone")]] int declared_required(int a);

// rules: STN-45
// expect: not-stone: uses variable 'g'
// expect-violation
int declared_required(int a) { return a - g; }

// Annoteringen på en template gäller varje instans.
// rules: STN-45, STN-6
// expect: id<int>: stone
template <typename T> [[clang::annotate("pork::stone")]] T id(T x) { return x; }

// rules: STN-45, STN-6
// expect: add_g<int>: not-stone: uses variable 'g'
// expect-violation: add_g<int>
template <typename T> [[clang::annotate("pork::stone")]] T add_g(T x) { return x + g; }

// rules: STN-23
// expect: stone
int use_id(int a) { return id(a); }

// rules: STN-23
// expect: not-stone: calls non-stone 'add_g<int>'
int use_add_g(int a) { return add_g(a); }
