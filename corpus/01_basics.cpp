// Grundfall: globaler, pekare, referenser, kontrollflöde.

int g = 0;
constexpr int limit = 10;

// rules: STN-1, STN-18
// expect: stone
int add(int a, int b) { return a + b; }

// rules: STN-22
// expect: not-stone: uses variable 'g' with static storage
int use_global(int a) { return a + g; }

// rules: STN-22
// expect: not-stone: uses variable 'g' with static storage
void write_global(int a) { g = a; }

// rules: STN-22
// expect: stone
int use_constexpr(int a) { return a < limit ? a : limit; }

// rules: STN-12
// expect: not-stone: parameter 'p' has type int *
void out(int* p) { *p = 1; }

// rules: STN-12
// expect: stone
int by_ref(const int& r) { return r; }

// rules: STN-9
// expect: not-stone: return type int *
int* returns_pointer() { return nullptr; }

// rules: STN-14
// expect: not-stone: local variable 'calls' has static storage
int counter() { static int calls = 0; return ++calls; }

// rules: STN-3, STN-14
// expect: not-stone: local variable 'x' has type volatile int
int volatile_local() { volatile int x = 1; return x; }

// rules: STN-13
// expect: not-stone: unsupported statement: GotoStmt
int with_goto(int a) { if (a > 0) goto done; a = -a; done: return a; }

// rules: STN-13
// expect: stone
int control_flow(int n) {
  int sum = 0;
  for (int i = 0; i < n; ++i) {
    if (i % 2 == 0) continue;
    sum += i;
  }
  while (sum > 100) sum -= 7;
  do { sum++; } while (sum < 3);
  switch (n) {
    case 0: return -1;
    case 1: break;
    default: sum *= 2;
  }
  return sum;
}

// rules: STN-1, STN-19
// expect: stone
double mixed(int a, float b, bool c, char d) { return c ? a * b : static_cast<double>(d); }

// &x är ett lån av x (lånevärde, lånemodellen följer det); bara == och !=.
// rules: STN-18, STN-62
// expect: stone
// loans: ok
int address_of_local() { int x = 1; int y = (&x == nullptr) ? 0 : 1; return y + x; }
