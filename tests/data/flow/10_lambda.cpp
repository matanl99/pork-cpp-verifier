// En lambda direkt som argument: den når omvärlden bara genom sina fångster.
// Fångst per värde läses, fångst per referens kan också skrivas, och anropet
// är okänd kod (allt den kan skriva beror på allt den läser).
#include <algorithm>
#include <variant>
#include <vector>

struct A { int v = 0; };
struct B { int w = 0; };
struct W {
  int total = 0;
  int other = 0;
};
// flow: w.total <- a.v, w.total (always written)
void apply(W& w, const A& a) { w.total += a.v; }
// flow: w.total <- b.w, w.total (always written)
void apply(W& w, const B& b) { w.total += b.w; }

// Fångst per referens: w kan skrivas, med allt som beroende.
// flow: w <- c, w (maybe written)
void step(W& w, const std::variant<A, B>& c) {
  std::visit([&w](const auto& x) { apply(w, x); }, c);
}
// Fångst per värde: läses.
// flow: v <- desc, v (maybe written)
void sort_by(std::vector<int>& v, bool desc) {
  std::ranges::sort(v, [desc](int a, int b) { return desc ? a > b : a < b; });
}
// Fångst per värde i ett predikat.
// flow: return <- limit, v
int count_over(const std::vector<int>& v, int limit) {
  return static_cast<int>(std::ranges::count_if(v, [limit](int x) { return x > limit; }));
}
// Fångst per referens som bara läses.
// flow: return <- limit, v
int count_over_ref(const std::vector<int>& v, const int& limit) {
  return static_cast<int>(std::ranges::count_if(v, [&limit](int x) { return x > limit; }));
}
// En lokal lambda utan fångster: anropet läser bara argumentet.
// flow: return <- x
int local_lambda(int x) {
  auto f = [](int y) { return y + 1; };
  return f(x);
}
// En lokal lambda som fångar per referens och skriver: anropet kan ändra out.
// flow: out <- out, x (maybe written)
void local_writer(int& out, int x) {
  auto add = [&out](int y) { out += y; };
  add(x);
}
// Fångst per värde gäller värdet när lambdan skapades, inte när den anropas.
// flow: return <- a
int captured_early(int a, int b) {
  auto f = [a] { return a; };
  a = b;
  a = 0;
  return f();
}
// En lokal lambda som argument.
// flow: v <- limit, v (maybe written)
void sort_local(std::vector<int>& v, int limit) {
  auto less = [limit](int p, int q) { return (p > limit) < (q > limit); };
  std::ranges::sort(v, less);
}
