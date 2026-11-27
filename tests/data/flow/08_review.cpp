// Granskning: tre fall där --flow tappade verkliga beroenden.
#include <span>
#include <vector>

// 1. Ett virtuellt anrop kan nå en override: okänd kod, inte basens flöde.
struct Shape {
  // flow: return <- nothing
  virtual int value(int) const { return 0; }
  // flow: not computed: destructor
  virtual ~Shape() = default;
};
struct Echo : Shape {
  // flow: return <- x
  int value(int x) const override { return x; }
};
// flow: return <- s, x
int ask(const Shape& s, int x) { return s.value(x); }
// Kvalificerat anrop: basens flöde är rätt.
// flow: return <- nothing
int ask_base(const Shape& s, int x) { return s.Shape::value(x); }

// 2. Skuggade fält är olika platser.
struct Base { int value = 0; };
struct Derived : Base { int value = 0; };
// flow: return <- d.Base::value
// flow: d.Derived::value <- x (always written)
int shadowed(Derived& d, int x) {
  d.value = x;
  return d.Base::value;
}

// 3. Ett element via en hjälpfunktion: svag skrivning, och indexet räknas.
// flow: out <- value (always written)
void set_value(int& out, int value) { out = value; }
// flow: return <- i, j, out[*], x
// flow: out[*] <- i, out[*], x (maybe written)
int element_via_call(std::vector<int>& out, int i, int j, int x) {
  set_value(out[i], x);
  return out[j];
}

// 4. (Granskning 2) Indexet följer platsen också genom en referens, en lokal
// referens och en metodmottagare, vid både läsning och skrivning.
struct Item {
  int v = 0;
  // flow: return <- this.v
  int get() const { return v; }
  // flow: this.v <- x (always written)
  void set(int x) { v = x; }
};
// flow: return <- r
int read_ref(const int& r) { return r; }
// flow: return <- i, v[*]
int via_call(const std::vector<int>& v, int i) { return read_ref(v[i]); }
// flow: return <- i, v[*]
int via_local(const std::vector<int>& v, int i) {
  const int& item = v[i];
  return item;
}
// Indexet gäller vid bindningen, inte efter att i ändrats.
// flow: return <- i, v[*]
int via_local_later(const std::vector<int>& v, int i) {
  const int& item = v[i];
  i = 0;
  return item + i;
}
// Ett const-anrop läser hela elementet.
// flow: return <- i, v[*]
int via_method(const std::vector<Item>& v, int i) { return v[i].get(); }
// flow: return <- i, v[*].v
int field_of_element(const std::vector<Item>& v, int i) { return v[i].v; }
// flow: v[*] <- i, v[*], x (maybe written)
void write_local(std::vector<int>& v, int i, int x) {
  int& e = v[i];
  e = x;
}
// flow: v[*].v <- i, v[*].v, x (maybe written)
void write_method(std::vector<Item>& v, int i, int x) { v[i].set(x); }

// 5. (Granskning 3) Läsning genom en skrivbar referens till ett element: också
// där beror det lästa på indexet.
// flow: return <- r
int read_mut(int& r) { return r; }
// flow: return <- i, v[*]
int via_mut_call(std::vector<int>& v, int i) { return read_mut(v[i]); }
struct Cell {
  int v = 0;
  // flow: return <- this.v
  int get() { return v; }
};
// flow: return <- i, v[*].v
int via_mut_method(std::vector<Cell>& v, int i) { return v[i].get(); }
// flow: out <- in (always written)
void copy_to(int& in, int& out) { out = in; }
// flow: out <- i, v[*] (always written)
void copy_element(std::vector<int>& v, int i, int& out) { copy_to(v[i], out); }

// 6. (Granskning 4) Anropet skriver också indexvariabeln: elementet valdes
// med i före anropet, så skrivningen beror på det gamla i. Namnen gör att
// i skrivs före elementet i sammanfattningen.
// flow: a <- nothing (always written)
// flow: b <- x (always written)
void update(int& a, int& b, int x) {
  a = 0;
  b = x;
}
// flow: i <- nothing (always written)
// flow: v[*] <- i, v[*], x (maybe written)
void reset_then_write(std::vector<int>& v, int& i, int x) { update(i, v[i], x); }

// 7. (Granskning 5) En referens som ett anrop returnerar pekar in i objektet:
// en senare skrivning syns genom den. Platsen följs inte än: not computed.
struct IntBox {
  int value = 0;
  // flow: return <- this.value
  const int& get() const { return value; }
  // flow: return <- this.value
  int copy() const { return value; }
};
// flow: not computed: reference bound to a reference returned by a call
int alias_via_getter(IntBox& box, int x) {
  const int& alias = box.get();
  box.value = x;
  return alias;
}
// Bunden direkt till fältet: platsen är känd.
// flow: return <- x
// flow: box.value <- x (always written)
int alias_direct(IntBox& box, int x) {
  const int& alias = box.value;
  box.value = x;
  return alias;
}
// Ett värderesultat bundet till const&: en temporär, alltså en kopia (const-
// metoden läser hela objektet).
// flow: return <- box
// flow: box.value <- x (always written)
int bound_copy(IntBox& box, int x) {
  const int& c = box.copy();
  box.value = x;
  return c;
}

// Range-for: loopvariabeln är ett element i behållaren (ett alias), inte en
// kopia. Loopvillkoret läser v (begin/end).
// flow: return <- v, v[*]
int sum(const std::vector<int>& v) {
  int s = 0;
  for (const int& e : v) s += e;
  return s;
}
// Skrivning genom en icke-const iterator följs inte (som förut).
// flow: not computed: call of '__gnu_cxx::__normal_iterator<int *, std::vector<int>>::operator*' changes an object that is not a known place
void fill_all(std::vector<int>& v, int x) {
  for (int& e : v) e = x;
}

// Över en std::span<const T>-parameter: elementen kan inte ändras.
// flow: return <- v
int sum_span(std::span<const int> v) {
  int s = 0;
  for (const int& e : v) s += e;
  return s;
}

// 8. (Beroendeoraklet i rugby) En sats med temporärer ligger i en
// ExprWithCleanups och föll bort ur flödet.
// flow: v <- k, v (maybe written)
void assign_count(std::vector<int>& v, int k) { v.assign(k, 0); }
// flow: out <- x (always written)
void assign_from_temp(int x, int& out) { out = read_ref(x + 1); }
