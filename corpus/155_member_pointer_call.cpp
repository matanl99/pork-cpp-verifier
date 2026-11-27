// Anrop genom en medlemspekare (STN-23): vilken funktion som körs syns inte i
// anropet, så det avvisas, också när pekaren är ett konstant uttryck och
// ingen variabel av medlemspekartyp finns.
struct C {
  int v;
  // rules: STN-23
  // expect: stone
  bool ok() const { return v > 0; }
};

// rules: STN-23
// expect: not-stone: call through a pointer to member
bool direct(const C& c) { return (c.*&C::ok)(); }

// rules: STN-23
// expect: not-stone: local variable 'pm' has type bool (C::*)() const
bool via_local(const C& c) {
  auto pm = &C::ok;
  return (c.*pm)();
}
