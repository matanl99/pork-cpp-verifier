// pork-args: --silver
//
// Enum-värden i Silver (SLV-11, SLV-22). En enum med fast typ kan ha vilket
// värde som helst av den typen, också ett som inte är uppräknat
// (`static_cast<Scoped>(7)`). En enum utan fast typ har bara värdena i sitt
// intervall (det minsta bitfältet som rymmer de uppräknade värdena), och att
// konvertera ett annat värde till den är odefinierat.

#include <pork/annotations.hpp>

enum class Scoped : int { A, B };
enum Two { Zero, One };            // intervall 0..1
enum Three { Red, Green, Blue };   // intervall 0..3: 3 är ett värde

// rules: SLV-11
// expect: stone: not silver: conversion to enum out of range at 100_silver_enums.cpp:17
Two bad_unfixed_cast(int n) { return static_cast<Two>(n); }

// rules: SLV-11
// expect: silver
Two ok_unfixed_constant() { return static_cast<Two>(1); }

// rules: SLV-11
// expect: silver
Three ok_unfixed_range(int n) {
  PORK_PRE(n >= 0 && n < 4);
  return static_cast<Three>(n);
}

// rules: SLV-11
// expect: stone: not silver: conversion to enum out of range at 100_silver_enums.cpp:34
Three bad_unfixed_range(int n) {
  PORK_PRE(n >= 0 && n < 5);
  return static_cast<Three>(n);
}

// rules: SLV-11
// expect: silver
Scoped ok_fixed_cast(int n) { return static_cast<Scoped>(n); }

// Indata: en Scoped-parameter kan vara 7.
// rules: SLV-22
// expect: stone: not silver: division by zero
int bad_input(Scoped s) {
  int d = s == Scoped::A ? 1 : s == Scoped::B ? 2 : 0;
  return 100 / d;
}

// rules: SLV-22
// expect: silver
int ok_input(Two t) {
  int d = t == Zero ? 1 : 2;
  return 100 / d;
}

// Ett kontrakt bevisas för alla värden, också de som inte är uppräknade.
// rules: SLV-18, SLV-22
// expect: stone: not silver: postcondition of 'bad_post'
int bad_post(Scoped s) {
  PORK_POST(int r, r != 0);
  if (s == Scoped::A) return 1;
  if (s == Scoped::B) return 2;
  return 0;
}

// rules: SLV-18
// expect: stone: not silver: relies on the unproven postcondition of 'bad_post'
int calls_bad_post(int n) { return 100 / bad_post(static_cast<Scoped>(n)); }

// Ett resultat från ett kontrakt kan vara vilket Scoped-värde som helst.
// rules: SLV-18, SLV-22
// expect: silver
Scoped pick(int n) {
  PORK_POST(Scoped r, true);
  return static_cast<Scoped>(n);
}

// rules: SLV-18, SLV-22
// expect: stone: not silver: division by zero
int uses_pick(int n) {
  Scoped s = pick(n);
  int d = 0;
  if (s == Scoped::A) d = 1;
  if (s == Scoped::B) d = 2;
  return 100 / d;
}
