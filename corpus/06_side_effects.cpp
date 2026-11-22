// Sidoeffekter (++, --, =, op=) får bara stå som en egen sats, som i SPARK.
// Inuti ett uttryck kan de ge osekvenserad modifiering, vilket är UB i C++
// (`i++ + i`), och gör dessutom ordningen beroende av utvärderingsordningen.

struct P { int x; };

// --- Underkända: sidoeffekt inuti ett uttryck ---

// rules: STN-20
// expect: not-stone: side effect inside expression: ++
int unsequenced(int i) { return i++ + i; }

// rules: STN-20
// expect: not-stone: side effect inside expression: ++
int return_increment(int i) { return i++; }

// rules: STN-20
// expect: not-stone: side effect inside expression: =
int chained(int a, int b) { a = b = 1; return a + b; }

// rules: STN-20
// expect: not-stone: side effect inside expression: =
int assign_in_condition(int a) { if ((a = a - 1)) return 1; return a; }

// rules: STN-20
// expect: not-stone: side effect inside expression: --
int while_decrement(int n) { int s = 0; while (n--) s += n; return s; }

// rules: STN-20
// expect: not-stone: side effect inside expression: ++
int init_with_increment(int a) { int b = a++; return a + b; }

// rules: STN-20
// expect: not-stone: side effect inside expression: +=
int nested_compound(int i) { i = i += 1; return i; }

// rules: STN-20
// expect: not-stone: side effect inside expression: =
P struct_assign_in_init(P a, P b) { P c = (a = b); return c; }

// rules: STN-20
// expect: not-stone: side effect inside expression: ++
int call_argument(int i) { return call_argument(i++); }

// --- Godkända: sidoeffekt som egen sats ---

// rules: STN-20
// expect: stone
int statements(int i) { i++; ++i; i--; i += 2; i = i * 2; (i)++; return i; }

// rules: STN-20
// expect: stone
int for_comma(int n) { int s = 0; for (int i = 0, j = n; i < j; ++i, --j) s += i * j; return s; }

// rules: STN-20
// expect: stone
int branch_statements(int a) { if (a > 0) a--; else a++; return a; }

// rules: STN-20
// expect: stone
P struct_assign_statement(P a, P b) { a = b; a.x += 1; return a; }
