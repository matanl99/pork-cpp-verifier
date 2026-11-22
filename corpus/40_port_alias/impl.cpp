// Implementationerna i en egen TU: anroparen kan inte se dem, Classify ser
// alla (STN-55).
#include "sink.hpp"

// En kontextreferens till Doc (som innehåller en Text).
struct DocSink final : ISink {
  // rules: STN-66, STN-53
  // expect: stone
  explicit DocSink(Doc& d) : doc_(d) {}
  // rules: STN-52
  // expect: stone
  void put(const Text& t) override { doc_.title.len = t.len; }
  // rules: STN-52
  // expect: stone
  void fix(Text& t) override { t.len = 0; }
  Doc& doc_;
};
