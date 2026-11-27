// Flytt av en väg (STN-96): efter std::move(p.namn) är p.namn giltig men
// ospecificerad. Resten av objektet (syskonfälten) får användas som vanligt;
// den flyttade vägen och allt som rymmer den (hela objektet) får inte
// användas förrän vägen tilldelats igen. Roten ska vara en lokal variabel
// eller värdeparameter, vägen bara fält (inga index), och typerna som rymmer
// vägen ska sakna egen destruktor. Kanske flyttad räknas som flyttad: en
// flytt i en gren eller loop följt av en användning underkänns.
#include <string>
#include <utility>
#include <vector>

struct Person {
  std::string namn;
  std::string stad;
  int alder;
};

struct Family {
  Person a;
  Person b;
};

// rules: STN-12
// expect: stone: assuming
int count(const Person& p) { return int(p.namn.size() + p.stad.size()); }

// rules: STN-96, STN-57
// expect: stone: assuming
int siblings(std::string& out) {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  person.stad += "s kommun";
  ++person.alder;
  out = namn;
  return person.alder + int(person.stad.size());
}

// rules: STN-96
// expect: stone: assuming
int field_reassigned() {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  person.namn = "Bo";
  return int(person.namn.size() + namn.size());
}

// rules: STN-96
// expect: stone: assuming
int whole_reassigned() {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  person = Person{"Bo", "Umeå", 1};
  return count(person) + int(namn.size());
}

// rules: STN-96, STN-57
// expect: stone: assuming
int from_value_parameter(Person p) {
  std::string n = std::move(p.namn);
  return p.alder + int(n.size() + p.stad.size());
}

// rules: STN-96
// expect: stone: assuming
int nested(Family f) {
  std::string n = std::move(f.a.namn);
  return count(f.b) + int(f.a.stad.size() + n.size());
}

// rules: STN-96
// expect: stone: assuming
int fresh_each_iteration(int n) {
  int total = 0;
  for (int i = 0; i < n; ++i) {
    Person p{"Anna", "Lund", i};
    std::string namn = std::move(p.namn);
    total += int(namn.size()) + p.alder;
  }
  return total;
}

// rules: STN-96
// expect: stone: assuming
int reassigned_in_loop(int n) {
  Person p{"Anna", "Lund", 30};
  std::string all;
  for (int i = 0; i < n; ++i) {
    all += std::move(p.namn);
    p.namn = "Bo";
  }
  return int(all.size() + p.namn.size());
}

// rules: STN-96
// expect: stone: assuming
int sibling_reference() {
  Person p{"Anna", "Lund", 30};
  std::string& stad = p.stad;
  std::string namn = std::move(p.namn);
  stad += "s kommun";
  return int(stad.size() + namn.size());
}

// rules: STN-96, STN-57
// expect: stone: assuming
int root_reassigned(std::string s) {
  std::string t = std::move(s);
  s = "x";
  return int(s.size() + t.size());
}

// rules: STN-96
// expect: not-stone: 'person.namn' is used after std::move(person.namn)
int read_moved() {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  return int(person.namn.size() + namn.size());
}

// rules: STN-96
// expect: not-stone: 'person' is used after std::move(person.namn)
int copy_whole() {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  Person q = person;
  return q.alder + int(namn.size());
}

// rules: STN-96
// expect: not-stone: 'person' is used after std::move(person.namn)
int pass_whole() {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  return count(person) + int(namn.size());
}

// rules: STN-96
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int moved_in_branch(bool c) {
  Person p{"Anna", "Lund", 30};
  std::string n;
  if (c) n = std::move(p.namn);
  return int(p.namn.size() + n.size());
}

// rules: STN-96
// expect: not-stone: std::move(p.namn) in a loop
int moved_in_loop(int n) {
  Person p{"Anna", "Lund", 30};
  std::string all;
  for (int i = 0; i < n; ++i) all += std::move(p.namn);
  return int(all.size());
}

// rules: STN-96
// expect: not-stone: 'r' is used after std::move(p.namn)
int reference_to_moved() {
  Person p{"Anna", "Lund", 30};
  std::string& r = p.namn;
  std::string namn = std::move(p.namn);
  return int(r.size() + namn.size());
}

// rules: STN-96
// expect: not-stone: 'f' is used after std::move(p.namn)
int lambda_after_move() {
  Person p{"Anna", "Lund", 30};
  auto f = [&] { return int(p.stad.size()); };
  std::string namn = std::move(p.namn);
  return f() + int(namn.size());
}

// rules: STN-96, STN-57
// expect: not-stone: 'f' is used after std::move(s)
int lambda_after_root_move(std::string s) {
  auto f = [&] { return int(s.size()); };
  std::string t = std::move(s);
  return f() + int(t.size());
}

// rules: STN-96
// expect: not-stone: 'person' is used after std::move(person.namn)
int captured_after_move() {
  Person person{"Anna", "Lund", 30};
  std::string namn = std::move(person.namn);
  auto f = [&person] { return person.alder; };
  return f() + int(namn.size());
}

// rules: STN-96, STN-57
// expect: not-stone: std::move of something other than a local variable or value parameter
int from_reference(Person& p) {
  std::string n = std::move(p.namn);
  return p.alder + int(n.size());
}

// rules: STN-96, STN-57
// expect: not-stone: 'v' is used after std::move(v[...].namn)
int from_element(std::vector<Person> v) {
  std::string n = std::move(v[0].namn);
  return int(v[1].stad.size() + n.size());
}

struct Named {
  std::string namn;
  std::string stad;
  // rules: STN-68
  // expect: stone: assuming
  ~Named() {}
};

// rules: STN-96
// expect: not-stone: std::move(n.namn) out of Named, which has a user-provided destructor
int own_destructor() {
  Named n{"Anna", "Lund"};
  std::string namn = std::move(n.namn);
  return int(n.stad.size() + namn.size());
}

// rules: STN-96
// expect: stone: assuming
int assigned_on_both_branches(bool c) {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  if (c) p.namn = "Bo";
  else p.namn = "Cy";
  return int(p.namn.size() + n.size());
}

// rules: STN-96
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int assigned_on_one_branch(bool c) {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  if (c) p.namn = "Bo";
  return int(p.namn.size() + n.size());
}

// rules: STN-96
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int appended_after_move() {
  Person p{"Anna", "Lund", 30};
  std::string n = std::move(p.namn);
  p.namn += "x";
  return int(p.namn.size() + n.size());
}

// rules: STN-96
// expect: stone: assuming
int two_fields() {
  Person p{"Anna", "Lund", 30};
  std::string a = std::move(p.namn);
  std::string b = std::move(p.stad);
  return p.alder + int(a.size() + b.size());
}

// rules: STN-96
// expect: not-stone: 'p.namn' is used after std::move(p.namn)
int same_field_twice() {
  Person p{"Anna", "Lund", 30};
  std::string a = std::move(p.namn);
  std::string b = std::move(p.namn);
  return int(a.size() + b.size());
}

// rules: STN-96, STN-84
// expect: stone: assuming
int binding_to_sibling() {
  Person p{"Anna", "Lund", 30};
  auto& [n, s, a] = p;
  std::string m = std::move(p.namn);
  return int(s.size() + m.size()) + a;
}

// rules: STN-96, STN-84
// expect: not-stone: 'n' is used after std::move(p.namn)
int binding_to_moved() {
  Person p{"Anna", "Lund", 30};
  auto& [n, s, a] = p;
  std::string m = std::move(p.namn);
  return int(n.size() + m.size());
}

struct Copied {
  std::string namn;
  std::string stad;
  // rules: STN-96, STN-66
  // expect: stone: assuming
  explicit Copied(Person p) : namn(std::move(p.namn)), stad(p.stad) {}
};

struct Reused {
  std::string namn;
  std::string stad;
  // rules: STN-96, STN-66
  // expect: not-stone: 'p.namn' is used after std::move(p.namn)
  explicit Reused(Person p) : namn(std::move(p.namn)), stad(p.namn) {}
};

struct Assigned {
  std::string namn;
  std::string stad;
  // rules: STN-72, STN-73
  // expect: stone: assuming
  Assigned& operator=(const Assigned& o) {
    namn = o.namn;
    stad = o.stad;
    return *this;
  }
};

// rules: STN-96, STN-72
// (En egen operator= kan läsa det flyttade fältet: ingen ny tilldelning.)
// expect: not-stone: 'w' is used after std::move(w.namn)
int own_assignment() {
  Assigned w{"Anna", "Lund"};
  std::string n = std::move(w.namn);
  w = Assigned{"Bo", "Umeå"};
  return int(w.namn.size() + n.size());
}

struct Household {
  Named n;
  int x;
};

// rules: STN-96
// expect: not-stone: std::move(h.n.namn) out of Named, which has a user-provided destructor
int inner_destructor() {
  Household h{{"Anna", "Lund"}, 1};
  std::string n = std::move(h.n.namn);
  return h.x + int(n.size());
}

#ifdef PORK_RUN
#include <pork/run.hpp>
PORK_RUN_CASES(
    {"siblings", [] { std::string out; return siblings(out) == 31 + 12 && out == "Anna" ? 0 : 1; }},
    {"field_reassigned", [] { return field_reassigned() == 6 ? 0 : 1; }},
    {"whole_reassigned", [] { return whole_reassigned() == 2 + 5 + 4 ? 0 : 1; }},
    {"from_value_parameter", [] { return from_value_parameter(Person{"Anna", "Lund", 30}) == 30 + 8 ? 0 : 1; }},
    {"nested", [] { return nested(Family{{"Anna", "Lund", 1}, {"Bo", "Umeå", 2}}) == 2 + 5 + 4 + 4 ? 0 : 1; }},
    {"fresh_each_iteration", [] { return fresh_each_iteration(3) == 12 + 3 ? 0 : 1; }},
    {"reassigned_in_loop", [] { return reassigned_in_loop(3) == 4 + 2 + 2 + 2 ? 0 : 1; }},
    {"sibling_reference", [] { return sibling_reference() == 12 + 4 ? 0 : 1; }},
    {"root_reassigned", [] { return root_reassigned("abc") == 4 ? 0 : 1; }},
    {"read_moved", [] { return read_moved() >= 4 ? 0 : 1; }},
    {"copy_whole", [] { return copy_whole() == 34 ? 0 : 1; }},
    {"pass_whole", [] { return pass_whole() >= 8 ? 0 : 1; }},
    {"moved_in_branch", [] { return moved_in_branch(true) >= 4 ? 0 : 1; }},
    {"moved_in_loop", [] { return moved_in_loop(2) >= 4 ? 0 : 1; }},
    {"reference_to_moved", [] { return reference_to_moved() >= 4 ? 0 : 1; }},
    {"lambda_after_move", [] { return lambda_after_move() == 8 ? 0 : 1; }},
    {"lambda_after_root_move", [] { return lambda_after_root_move("abc") >= 3 ? 0 : 1; }},
    {"captured_after_move", [] { return captured_after_move() == 34 ? 0 : 1; }},
    {"own_destructor", [] { return own_destructor() == 8 ? 0 : 1; }},
    {"assigned_on_both_branches", [] { return assigned_on_both_branches(true) == 6 && assigned_on_both_branches(false) == 6 ? 0 : 1; }},
    {"assigned_on_one_branch", [] { return assigned_on_one_branch(false) >= 4 ? 0 : 1; }},
    {"appended_after_move", [] { return appended_after_move() >= 5 ? 0 : 1; }},
    {"two_fields", [] { return two_fields() == 38 ? 0 : 1; }},
    {"same_field_twice", [] { return same_field_twice() >= 4 ? 0 : 1; }},
    {"binding_to_sibling", [] { return binding_to_sibling() == 38 ? 0 : 1; }},
    {"binding_to_moved", [] { return binding_to_moved() >= 4 ? 0 : 1; }},
    {"own_assignment", [] { return own_assignment() == 6 ? 0 : 1; }},
    {"inner_destructor", [] { return inner_destructor() == 5 ? 0 : 1; }})
#endif
