// pork-args: --silver
//
// Strängliteral som argument till en betrodd funktion med en `const char*`-
// parameter (loggning, utskrift): `print_line("klart")`. Literalen är
// oföränderlig och lever hela programmet, så att skicka den läser bara
// konstant data. Bara direkt som argument: inga pekarvariabler, ingen
// pekararitmetik, ingen jämförelse. En Stone-funktion kan inte själv ha en
// `const char*`-parameter, så mottagaren är alltid betrodd.
//
// Silver: literalen blir en C-literal, kontraktet heter
// `print_line__const_char_ptr` (32_string_literal.contracts.c).

void print_line(const char* s);         // betrodd (32_string_literal.trust)
void print_twice(const char* s, int n);  // betrodd, kontrakt med förvillkor
void untrusted_print(const char* s);     // inte betrodd

// rules: STN-29, SLV-28
// expect: silver: assuming print_line
void hello() { print_line("hello"); }

// rules: STN-29, SLV-28
// expect: silver: assuming print_line
int report(int x) {
  if (x > 10) print_line("x is \"large\"\n");
  return x;
}

// rules: SLV-28, SLV-21
// expect: stone: not silver: precondition of 'print_twice'
void twice_any(int n) { print_twice("tick", n); }

// rules: SLV-28
// expect: silver: assuming print_twice
void twice_ok(int n) { print_twice("tick", n < 0 ? 0 : n); }

// rules: STN-23
// expect: not-stone: calls 'untrusted_print' with no definition
void via_untrusted() { untrusted_print("hello"); }

// En egen funktion med `const char*`-parameter är aldrig Stone.
// rules: STN-12
// expect: not-stone
void own_print(const char* s) { (void)s; }

// rules: STN-23
// expect: not-stone: calls non-stone 'own_print'
void via_own() { own_print("hello"); }

// Pekaren som värde: inte bara som argument.
// rules: STN-29
// expect: not-stone
void pointer_local() {
  const char* p = "hello";
  print_line(p);
}

// rules: STN-29
// expect: not-stone
void pointer_arithmetic() { print_line("hello" + 1); }

// rules: STN-29
// expect: not-stone
void pointer_choice(bool b) { print_line(b ? "yes" : "no"); }

// rules: STN-29
// expect: not-stone
bool same_literal() { return "a" == "a"; }
