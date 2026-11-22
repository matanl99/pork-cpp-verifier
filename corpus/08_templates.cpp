// Templates klassas per instans, som generics i SPARK. Instanser som används
// i koden klassas automatiskt; andra kan tvingas fram med en explicit
// instansiering. Själva mönstret rapporteras inte.
//
// Flera instanser delar källrad, så förväntningarna namnger funktionen.

int g = 0;

struct Vec2 { int x; int y; };
struct Holder { int* p; };

// rules: STN-6
// expect: identity<int>: stone
// expect: identity<Vec2>: stone
// expect: identity<Holder>: not-stone: return type Holder (field 'p' of Holder has type int *)
template <typename T> T identity(T x) { return x; }

// rules: STN-6
// expect: twice<int>: stone
// expect: twice<double>: stone
template <typename T> T twice(T x) { return x + x; }

// rules: STN-6, STN-22
// expect: add_global<int>: not-stone: uses variable 'g'
template <typename T> T add_global(T x) { return x + g; }

// Aldrig instansierad: inget besked alls (testet fallerar om en rad dyker upp).
template <typename T> T never_used(T x) { return x; }

// rules: STN-6, STN-23
// expect: stone
int use_identity(int a) { return identity(a) + twice(a); }

// rules: STN-6, STN-23
// expect: stone
Vec2 use_identity_struct(Vec2 v) { return identity(v); }

// rules: STN-6, STN-23
// expect: not-stone: calls non-stone 'add_global<int>'
int use_add_global(int a) { return add_global(a); }

// Explicita instansieringar: typer som inte används ovan.
template double twice<double>(double);
template Holder identity<Holder>(Holder);

template <typename T>
struct Box {
  T value;
  // rules: STN-6, STN-7
  // expect: Box<int>::get: stone
  // expect: Box<Holder>::get: not-stone: member of non-object type Box<Holder>
  T get() const { return value; }
  // rules: STN-6, STN-7
  // expect: Box<int>::set: stone
  // expect: Box<Holder>::set: not-stone: member of non-object type Box<Holder>
  void set(T v) { value = v; }
};

// rules: STN-6, STN-23
// expect: stone
int use_box(Box<int> b) { return b.get(); }

// rules: STN-6, STN-21
// expect: stone
int use_box_set(Box<int> b) { b.set(1); return b.value; }

template struct Box<Holder>;
