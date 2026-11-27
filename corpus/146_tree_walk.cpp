// En pekare som går nedåt i en rekursiv struktur i en loop (`p = &p->kids[0]`):
// varje varv ger en djupare plats (root.kids[*].kids[*]...), och lånemodellens
// fixpunkt nåddes aldrig. En väg längre än en gräns kortas nu av till "någonstans
// i den ägda lagringen under prefixet": den kortare platsen överlappar allt den
// ersätter, så en ändring som kan ogiltigförklara pekaren gör det fortfarande.
#include <cstddef>
#include <vector>

struct Node {
  std::vector<Node> kids;
  int value = 0;
};

// rules: STN-62
// expect: stone: assuming
int depth(const Node& root) {
  int n = 0;
  const Node* p = &root;
  while (!p->kids.empty()) {
    p = &p->kids[0];
    ++n;
  }
  return n;
}

// rules: STN-62
// expect: stone: assuming
int leaf_value(const Node& root) {
  const Node* p = &root;
  while (!p->kids.empty()) p = &p->kids.back();
  return p->value;
}

// Roten töms medan p pekar in i den: p dinglar.
// rules: STN-62
// expect: not-stone: loans:
int cleared_root(Node& root) {
  const Node* p = &root;
  while (!p->kids.empty()) p = &p->kids[0];
  root.kids.clear();
  return p->value;
}

// En nod djupare än gränsen växer: p kan peka in i den.
// rules: STN-62
// expect: not-stone: loans:
int grown_deep(Node& root) {
  const Node* p = &root;
  while (!p->kids.empty()) p = &p->kids[0];
  Node& a = root.kids[0];
  Node& b = a.kids[0];
  Node& c = b.kids[0];
  Node& d = c.kids[0];
  Node& e = d.kids[0];
  Node& f = e.kids[0];
  f.kids.push_back(Node{});
  return p->value;
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"depth", [] {
       Node r;
       r.kids.push_back(Node{});
       r.kids[0].kids.push_back(Node{});
       return depth(r) == 2 ? 0 : 1;
     }},
    {"leaf_value", [] {
       Node r;
       r.kids.push_back(Node{{}, 7});
       return leaf_value(r) == 7 ? 0 : 1;
     }})
#endif
