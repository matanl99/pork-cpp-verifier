// Ingen läsning av skräpvärden. Structar måste initieras där de deklareras
// (helt), och ingen variabel får läsas i sin egen initierare. En skalär
// (heltal, flyttal, bool, enum) får deklareras utan initierare om den tilldelas
// med = före varje läsning på alla vägar (clangs analys av oinitierade värden,
// som -Wsometimes-uninitialized). Den får då aldrig bindas till en referens
// eller fångas i en lambda: clang räknar ett T&-argument som initiering.

struct Vec2 { int x; int y; };

// --- Underkända ---

// rules: STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int read_before_assign() { int x; int y = x + 1; x = 1; return y; }

// rules: STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int assigned_on_one_path(bool b) { int x; if (b) x = 1; return x; }

// rules: STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int assigned_in_loop(int n) { int x; for (int i = 0; i < n; ++i) x = i; return x; }

// rules: STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int increment_unassigned() { int x; x++; return 0; }

// rules: STN-17
// expect: not-stone: local variable 'x' may be read before it is assigned
int compound_unassigned() { int x; x += 1; return 0; }

// rules: STN-12
// expect: stone
void set(int& out) { out = 1; }
// rules: STN-17
// expect: not-stone: local variable 'x' without initializer is bound to a reference
int through_reference() { int x; set(x); return x; }

// rules: STN-12
// expect: stone
int get(const int& in) { return in; }
// rules: STN-17
// expect: not-stone: local variable 'x' without initializer is bound to a reference
int const_reference_after_assign() { int x; x = 2; return get(x); }

// rules: STN-17
// expect: not-stone: local variable 'x' without initializer is bound to a reference
int local_reference() { int x; x = 1; int& r = x; return r; }

// rules: STN-17, STN-40
// expect: not-stone: local variable 'x' without initializer is captured by a lambda
int captured() { int x; x = 1; auto f = [x] { return x; }; return f(); }

// rules: STN-15
// expect: not-stone: local variable 'v' is not initialized
int uninit_struct() { Vec2 v; v.x = 1; v.y = 2; return v.x; }


// rules: STN-16
// expect: not-stone: local variable 'x' is used in its own initializer
int self_init() { int x = x + 1; return x; }

// rules: STN-16
// expect: not-stone: local variable 'v' is used in its own initializer
int self_init_struct() { Vec2 v{1, v.x}; return v.y; }

// --- Godkända ---

// rules: STN-14
// expect: stone
int init_scalar() { int x = 0; int y{}; int z = int(); return x + y + z; }

// rules: STN-15
// expect: stone
int init_struct() { Vec2 v{}; Vec2 w{1}; Vec2 u = Vec2(); Vec2 c = v; return v.x + w.y + u.x + c.y; }

// rules: STN-14
// expect: stone
int init_for(int n) { int s = 0; for (int i = 0; i < n; ++i) s += i; return s; }

// rules: STN-17
// expect: stone
int assign_then_read() { int x; x = 1; return x; }

// rules: STN-17
// expect: stone
int assigned_on_both_paths(bool b) { int x; if (b) x = 1; else x = 2; return x + 1; }

// rules: STN-17
// expect: stone
int assign_then_modify() { unsigned x; x = 0u; x++; x += 2u; return int(x); }

enum class Mode { a, b };
// rules: STN-17
// expect: stone
int enum_and_float(bool b) { Mode m; float f; m = b ? Mode::a : Mode::b; f = 1.5F; return m == Mode::a ? int(f) : 0; }

// rules: STN-17
// expect: stone
int never_read(int n) { int s = 0; for (int i; s < n;) s += 1; return s; }
