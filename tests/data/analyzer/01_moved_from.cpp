// Självtest för tests/check_analyzer.py. Båda funktionerna är Stone, och CSA
// (cplusplus.Move) varnar för metodanropet på den flyttade unique_ptr:n
// (falskt: en flyttad unique_ptr är tom, [unique.ptr.single.ctor]).
// `unmarked` ska ge HOLE (kedjan pork-dom -> varning -> klass fungerar),
// `marked` ska ge false (markeringen läses).
#include <memory>
#include <utility>

struct R {
  int value;
};

int take(std::unique_ptr<R> p) { return p->value; }

int unmarked() {
  auto p = std::make_unique<R>(R{1});
  int n = take(std::move(p));
  return p.get() == nullptr ? n : 0;
}

int marked() {
  auto p = std::make_unique<R>(R{1});
  int n = take(std::move(p));
  // analyzer: cplusplus.Move: a moved-from unique_ptr is null
  return p.get() == nullptr ? n : 0;
}
