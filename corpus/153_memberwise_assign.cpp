// Tilldelning av ett objekt som bär lån (STN-105). En implicit eller defaulted
// operator= kopierar eller flyttar fält för fält: en vy eller pekare får ett nytt
// värde, men inget skrivs genom det den pekar på. Två operander som lånar samma
// källa krockar därför inte (G3), om ingen användarkod körs i operationerna
// (egna specialmedlemmar i klassen, dess baser, fält eller std-mallarnas
// typargument).
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct Flat {
  int k;
  std::string_view name;
};

// rules: STN-62, STN-105
// expect: stone: assuming
void reset_flat(Flat& value, std::string_view name) { value = Flat{0, name}; }

struct Deep {
  std::vector<Deep> kids;
  std::string_view name;
};

// rules: STN-62, STN-105
// expect: stone: assuming
void reset_deep(Deep& value, std::string_view name) { value = Deep{{}, name}; }

// Ett träd som byggs nedifrån: båda sidor lånar name.
// rules: STN-62, STN-105
// expect: stone: assuming
int depth_of_built(std::string_view name, int n) {
  Deep value{{}, name};
  for (int i = 0; i < n; ++i) {
    Deep next{{}, name};
    next.kids.push_back(value);
    value = next;
  }
  int d = 0;
  for (const Deep* p = &value; !p->kids.empty(); p = &p->kids[0]) ++d;
  return d;
}

struct Window {
  std::span<int> data;
};

// rules: STN-62, STN-105
// expect: stone: assuming
void rebind(Window& w, std::span<int> t) { w = Window{t}; }

// En egen operator= med kropp bedöms av sin egen lånesammanfattning (STN-62),
// inte av STN-105: att skriva ett int genom en vy som kan överlappa källan är
// ingen krock.
struct Writes {
  std::span<int> data;
  // rules: STN-72
  // expect: stone: assuming
  Writes& operator=(const Writes& o) {
    if (!data.empty() && !o.data.empty()) data[0] = o.data[0];
    return *this;
  }
};

// rules: STN-62
// expect: stone: assuming
void own_assign(Writes& w, std::span<int> t) { w = Writes{t}; }

// En std-mall utan egen sammanfattning som kör användarkod (optional<Writes>
// anropar Writes::operator=): STN-105 gäller inte, G3 avvisar som förut.
struct Holder {
  std::optional<Writes> w;
};

// rules: STN-62, STN-105
// expect: not-stone: loans: operands
void wrapped_user_assign(Holder& h, std::span<int> t) { h = Holder{Writes{t}}; }

// Samma mall utan användarkod i typargumentet.
struct Plain {
  std::optional<std::span<int>> s;
};

// rules: STN-62, STN-105
// expect: stone: assuming
void wrapped_view(Plain& p, std::span<int> t) { p = Plain{t}; }

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"reset_flat", [] {
       Flat f{1, "a"};
       reset_flat(f, "bc");
       return f.k == 0 && f.name == "bc" ? 0 : 1;
     }},
    {"reset_deep", [] {
       Deep d{{Deep{{}, "x"}}, "a"};
       reset_deep(d, "b");
       return d.kids.empty() && d.name == "b" ? 0 : 1;
     }},
    {"depth_of_built", [] { return depth_of_built("n", 3) == 3 ? 0 : 1; }},
    {"rebind", [] {
       int a[2] = {1, 2};
       int b[3] = {4, 5, 6};
       Window w{a};
       rebind(w, b);
       return w.data.size() == 3 && w.data[0] == 4 ? 0 : 1;
     }},
    {"wrapped_view", [] {
       int a[2] = {1, 2};
       Plain p{};
       wrapped_view(p, a);
       return p.s && p.s->size() == 2 ? 0 : 1;
     }})
#endif
