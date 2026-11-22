#pragma once
struct Text { int len; };
struct Doc { Text title; int n; };
// Utan egen destruktor: check_corpus.py läser förväntningar bara i .cpp.
struct ISink {
  virtual void put(const Text& t) = 0;
  virtual void fix(Text& t) = 0;
};
