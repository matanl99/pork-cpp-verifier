# Silver: från C++ till C för CBMC

Pork översätter varje Stone-funktion till C och låter en bevisare (CBMC som standard,
eller ESBMC) bevisa C-programmet. Clang har redan löst upp typer, överlagringar,
mallinstanser och implicita konverteringar, så översättningen kan arbeta på det typade
AST:t. Båda bevisarna får samma C-modell, med samma kontrakt och gränser.

## Översättningen (`adapters/clang/CTranslate.cpp`)

Översättningen utgår från clangs AST, inte från källtexten: typer, implicita
konverteringar och vilken överlagring som anropas är redan avgjorda. Det fungerar
eftersom Stone har begränsat språket till något som har en direkt motsvarighet i C.
Allt annat ger `cannot translate to C: ...`, aldrig en gissning.

| C++ | C |
|---|---|
| funktion, metod, template-instans | `f_<kvalificerat namn>_<hash av USR>`: unikt per överlagring/instans, samma i alla TU:er |
| värdestruct | `struct s_<namn>_<hash> { fält }`, deklarerad efter de typer den använder |
| `bool`, enum | `_Bool`, enumens underliggande heltalstyp |
| `std::optional<T>` (betrodd typ) | `struct { _Bool has; T v; }`, operationerna modelleras direkt (se nedan) |
| heltalskonvertering som kan tappa värdet (bara `--silver-strict`) | `pork_<från>_to_<till>_checked(x)`: assert att värdet ryms, sedan C-casten |
| strängliteral till betrodd `const char*`-parameter | C-literalen (oktala escapes); kontraktets parameter heter `const_char_ptr` |
| `const T&`-parameter | värde (Stone garanterar att inget annat ändrar objektet under anropet) |
| `T&`-parameter | `T *`, användningar blir `(*p)` |
| `std::span<const T>`-parameter | `struct { unsigned long size; const T *data; }`; från en vektor `{v.size, v.data}`, i harnessen en egen array med N element och `size <= N`; `s[i]`/`front`/`back` kontrollerar indexet |
| `this` | `self`: värde i const-metoder, pekare annars; `n` blir `self.n` / `self->n` |
| lokal referens till en väg | pekare; till en temporär: vanlig lokal |
| implicita konverteringar (`CK_IntegralCast` m.fl.) | uttryckliga C-casts, så befordran och aritmetik blir som i C++ |
| flyttal → heltal | hjälpfunktion som kontrollerar att värdet ryms (annars UB) |
| `{a.x + b.x, ...}` | `(struct T){...}`; fältinitierare (`float x{0.0F}`) är redan ifyllda av clang |
| `constexpr`, enum-värden | literaler, uträknade av clang |
| anrop till analyserad funktion | dess C-namn |
| anrop till betrodd funktion | kontrakt: `std::hypot(float, float)` → `std__hypot__float__float` (`contracts/std.c`) |
| `PORK_PRE(c)` | `__CPROVER_assert(c, "precondition of 'f'")`; i `f__entry` i stället `__CPROVER_assume(c)` |
| varje sats | föregås av `#line N "fil.cpp"`, så fynden pekar in i C++-källan |

Varje funktion skrivs ut två gånger: den vanliga (förvillkoret kontrolleras vid varje
anrop) och `__entry` (förvillkoret antas), som bara anropas av harnessen:

```c
void pork_harness(void) {
  int pork_a0;                       // oinitierad = godtyckligt värde för CBMC
  float pork_a1;
  __CPROVER_assume(!__CPROVER_isnanf(pork_a1) && !__CPROVER_isinff(pork_a1));
  f_example_1234abcd__entry(pork_a0, pork_a1);
}
```

Exempel (`corpus/20_silver.cpp`, `#line` borttagna):

```cpp
struct Counter { int n;
  void inc_pre() { PORK_PRE(n < 1000); n = n + 1; }
  int get() const { return n; } };
int count_twice(int start) { PORK_PRE(start >= 0 && start < 100);
  Counter c{start}; c.inc_pre(); c.inc_pre(); return c.get(); }
```
```c
struct s_Counter_22d84325 { int n; };
void f_Counter__inc_pre_8cab4bda(struct s_Counter_22d84325 *self) {
  __CPROVER_assert((self->n < 1000), "precondition of 'Counter::inc_pre'");
  (self->n = (self->n + 1)); }
int f_Counter__get_6a9ff3f3(struct s_Counter_22d84325 self) { return self.n; }
int f_count_twice_9e792503__entry(int start) {
  __CPROVER_assume(((start >= 0) && (start < 100)));
  struct s_Counter_22d84325 c = (struct s_Counter_22d84325){start};
  f_Counter__inc_pre_8cab4bda(&c); f_Counter__inc_pre_8cab4bda(&c);
  return f_Counter__get_6a9ff3f3(c); }
```

Kärnan (`core/Silver.cpp`) sätter ihop programmet för en funktion: typerna en gång
var, alla prototyper, definitionerna för allt som anropas transitivt och sist rotens
`__entry` med harness. `adapters/cbmc/` kör `cbmc` som process och läser `--json-ui`.
`--keep-c <katalog>` sparar programmen.

Kvar att mappa tillbaka: uttryckstexten i CBMC:s meddelanden är C (`self->n + 1`).

## `std::optional<T>`

Betrodd typ (`type std::optional` och `std::optional::*` i trust-filen). Operationerna
översätts till en egen modell i stället för att analyseras; den modellen är en del av
det som är betrott och redovisas som `assuming std::optional::...`:

| C++ | C |
|---|---|
| `std::optional<T> o;`, `std::nullopt`, `{}` | `{0}` (has = 0) |
| `std::optional<T> o = x;`, `return x;` | `{1, x}` |
| `o = x`, `o = std::nullopt`, `o.reset()` | tilldelning av `{1, x}` / `{0}` |
| `if (o)`, `o.has_value()` | `o.has` |
| `*o`, `o.value()` | `o.v` efter `assert(o.has)` ("precondition of 'std::optional::operator*'") |
| `o.value_or(d)` | `o.has ? o.v : d` |

## `std::vector<T>`

Betrodd typ (`type std::vector`, `type std::allocator`, `std::vector::*`). Modellen är
begränsad: `struct { unsigned long size; T data[N]; }` med N = `--vector-bound`
(standard 16). Harnessen antar `size <= N`, och ett bevis redovisas som
`assuming std::vector size <= N`. Det är samma slags gräns som `--unwind`.

| C++ | C |
|---|---|
| `std::vector<T> v{}`, kopiering, `v = w` | `{0}`, struct-kopia, tilldelning |
| `v.size()`, `v.empty()` | `v.size`, `v.size == 0` |
| `v[i]`, `v.at(i)` | `v.data[i]` efter `assert(i < v.size)` |
| `v.front()`, `v.back()` | `v.data[0]` / `v.data[v.size - 1]` efter `assert(v.size > 0)` |
| `v.push_back(x)` | `assert(v.size < N)` ("may grow beyond N elements"), sedan `data[size++] = x` |
| `v.pop_back()`, `v.clear()` | `assert(v.size > 0)` och `size - 1`; `size = 0` |
| `v.assign(n, x)` | `assert(n <= N)` ("may grow beyond N elements"), sedan hjälparen `<typ>__assign`: `data[0..n) = x`, `size = n`. Bara antal + värde, inte iteratorer/initieringslista |
| `v.resize(n)`, `v.resize(n, x)` | `assert(n <= N)`, sedan hjälparen `<typ>__resize`: `data[size..n) = x`, `size = n` (krympning ändrar bara `size`). Utan `x` värdeinitieras elementet: fältinitierare, annars noll (`valueInit`) |
| `Player& p = v[i];` | `assert(i < v.size); struct Player *p = &v.data[i];` |

Lokala referenser till element (även `*o` i en optional) blir pekare. Stones lånregler
garanterar att vektorn inte ändras medan referensen lever, så pekaren är giltig.

`std::string` översätts som en `std::vector<char>` med samma gräns (se
[den svenska referensen](reference.sv.md#stdstring)), och `std::variant` beskrivs [nedan](#stdvariant-get_if-och-lambdor).
Av standardbibliotekets typer översätts bara `std::optional`, `std::vector`, `std::string`,
`std::variant` och `std::span<const T>`. Andra betrodda typer (t.ex. `std::map`,
`std::array`, `std::unique_ptr`) ger "trusted type ... is not supported yet".

## ESBMC som bevisare

[ESBMC](https://github.com/esbmc/esbmc) används med `--silver-prover=esbmc`
(`adapters/esbmc/EsbmcProver`). Adaptern får samma C-program och kontrakt som CBMC. En liten
header mappar `__CPROVER_assert`/`assume`/`isnan`/`isinf` till ESBMC:s motsvarigheter, och en
funktion utan kropp räknas alltid som ett fel (`no contract for ...`), som hos CBMC.
Standardlösaren är bitwuzla. Korpusen körs med båda bevisarna. Mätningar och detaljer står i
[den svenska referensen](reference.sv.md#bevisare---silver-proveresbmc). ESBMC:s C++-frontend används dessutom i ekvivalensprototypen
([equivalence.md](equivalence.md)).

## Åtkomst via accessorer, range-for, standardkonstruktor

| C++ | C |
|---|---|
| `v[i]` (väg) | `(*(__CPROVER_assert(i < v.size, ...), &v.data[i]))` (ett lvalue: går att skriva till) |
| `*o`, `o.value()` (väg) | `(*(__CPROVER_assert(o.has, ...), &o.v))` |
| `o->f` | `(*(__CPROVER_assert(o.has, "...operator->..."), &o.v)).f` |
| `o = p` (optional = optional) | `o = p` |
| `for (T& x : v) body` | `for (x__i = 0; x__i < v.size; x__i++) { T *x = &v.data[x__i]; body }` |
| `for (T x : v) body` | samma, med `T x = v.data[x__i];` |
| `(void)f(x)` | `((void)f(x))` |
| `World w;` (implicit konstruktor) | `(struct World){fältinitierare..., {0} för optional/vector}` |

Stone garanterar att `v` inte ändras i loopen (lånet), så `v.size` är fast.

## Anrop mellan TU:er

En egen funktion som bara är deklarerad i TU:n anropas med sitt vanliga C-namn
(`f_<namn>_<hash av USR>`), som är detsamma i alla TU:er. `core/Silver.cpp` tar
med översättningen från den TU där den är definierad. Finns ingen (betrodd via
trust-filen, eller saknas helt) blir namnet `#define`:at till kontraktet.

## std::variant, get_if och lambdor

| C++ | C |
|---|---|
| `std::variant<A, B>` | `struct { unsigned long index; union { A a0; B a1; } u; }` |
| `v.index()`, `holds_alternative<B>(v)` | `v.index`, `(v.index == 1ul)` |
| `std::get<B>(v)` | `(*(__CPROVER_assert(v.index == 1ul, ...), &v.u.a1))` |
| `Command c = PassTo{...}` | `(struct ...){1ul, {.a1 = ...}}` |
| `const B* p = std::get_if<B>(&v)` | `const B *p = v.index == 1ul ? &v.u.a1 : 0;` (`--pointer-check`) |
| `auto f = [&x, y](int a) {...}` | `static R f__l0(X *x, Y y, int a) {...}`; kopian av `y` tas vid skapandet |
| `std::visit(f, v)` | `(v.index == 0ul ? f__l0(..., v.u.a0) : f__l1(..., v.u.a1))` |

I harnessen antas `index` < antalet alternativ, och bara det aktiva alternativet antas giltigt.
