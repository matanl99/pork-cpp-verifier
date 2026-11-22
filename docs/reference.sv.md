# Pork

Pork är ett experiment i formell verifiering av C++. Verktyget analyserar
funktioner med Clang och kan kontrollera dataflöde och bevisa frånvaro av
vissa körtidsfel. Projektet startades av Mathias Anlér och är inspirerat av
[SPARK:s verifieringsnivåer](https://docs.adacore.com/spark2014-docs/html/ug/en/usage_scenarios.html).
Pork har ingen koppling till AdaCore eller SPARK-projektet.

| Nivå | Vad Pork kontrollerar |
|---|---|
| **Pork Stone** | Funktionen och dess anrop ligger inom en analyserbar C++-delmängd med regler för initiering, sidoeffekter, alias och livstider. [Lånemodell](stone_model.md) · [Specifikation](../spec/1_stone.txt) |
| **Pork Bronze** | En Stone-funktion uppfyller sina deklarerade flödeskrav. `PORK_DEPENDS` anger vilka indata en utdata får bero på. Utan sådana krav tillkommer ingen flödesförpliktelse. [Guide](bronze.md) · [Specifikation](../spec/3_bronze.txt) |
| **Pork Silver** | Pork bevisar frånvaro av de körtidsfel som kontrolleras i C-modellen, under funktionens förvillkor och redovisade modellantaganden. En körning som kan överskrida utrullningsgränsen spärrar Silver, liksom ett brutet Bronze-krav. [Guide](silver.md) · [Specifikation](../spec/2_silver.txt) |

## Bygga och köra

Bygget kräver CMake 3.28 eller senare, en C++23-kompilator och LLVM/Clang 23
med utvecklingsbibliotek och CMake-konfiguration. Ha en matchande `clang` på
`PATH`. GoogleTest och Python 3 krävs vid konfigureringen eftersom bygget
även konfigurerar testerna. Kommandot nedan använder Ninja.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

För Silver behövs [CBMC](https://github.com/diffblue/cbmc) 6.x på `PATH` som
standard. [ESBMC](https://github.com/esbmc/esbmc) kan väljas med
`--silver-prover=esbmc`. Stone och Bronze behöver ingen av bevisarna.

### Första körningen

Spara följande som `example.cpp` i Porks rotkatalog.

```cpp
#include <pork/annotations.hpp>

int ratio(int x) {
  PORK_PRE(x > 0);
  return 100 / x;
}
```

```sh
./build/pork example.cpp -- -std=c++17 -Iinclude
./build/pork --silver example.cpp -- -std=c++17 -Iinclude
```

Första kommandot rapporterar `ratio: stone`. Det andra rapporterar
`ratio: silver`. Förvillkoret utesluter division med noll. Efter `--` står
flaggorna för den analyserade koden. Välj den C++-standard som koden faktiskt
byggs med. `PORK_DEPENDS` använder `__VA_OPT__`, som standardiserades i C++20.

### Analysera ett befintligt projekt

För ett CMake-projekt med Ninja kan du generera en kompileringsdatabas och
sedan köra Pork från målprojektets rot.

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/path/to/pork/build/pork -p build src/main.cpp src/core/*.cpp
```

`-p build` pekar på katalogen som innehåller `compile_commands.json`.
Fillistan väljer vilka översättningsenheter som ska analyseras. Ange alla
relevanta källfiler för att täcka hela projektet. Lägg till `--flow` för att
visa det härledda dataflödet eller `--silver` för att köra körtidsbevisen.
Använd `--help` för fler flaggor.

### Tester och licens

Kör `ctest --test-dir build --output-on-failure` från Porks rot. Se
[korpusens README](../corpus/README.md) för hur testfallen kopplas till reglerna.
För ett bygge med ASan och UBSan finns
`./cmasan.sh && ./blasan.sh && ./ttasan.sh`.

Pork publiceras under [Mozilla Public License 2.0](../LICENSE). Copyright 2026 Mathias Anlér.

Utdata, en rad per funktion (kravbrott på stderr):

```
fil:rad:kol: namn: silver[: assuming a, b]                 (--silver)
fil:rad:kol: namn: stone: not silver: fynd at fil:rad      (--silver)
fil:rad:kol: namn: stone
fil:rad:kol: namn: stone: assuming a, b
fil:rad:kol: namn: trusted
fil:rad:kol: namn: not-stone: orsak
```

## Stone v1

**Värdetyper:** heltal (inkl. `bool` och `char`), flyttal och enum, samt
- structar utan basklasser, virtuella funktioner, `mutable`-fält och unioner, där alla fält
  är värdetyper och typen antingen är trivialt kopierbar eller saknar egna kopierings-,
  flytt- och destruktorfunktioner. En kopia är då en medlemsvis kopia av värden,
- **betrodda typer**: en instans av en klasstemplate som står som `type` i trust-filen
  (`type std::optional`), när alla dess typargument är värdetyper. Det redovisas som
  antagandet `type std::optional`.

Inget får vara `volatile`. Pekare, referenser och arrayer är inga värdetyper.

En funktionsdefinition är Stone om

- den inte är variadisk. Templates klassas **per instans**, som generics i SPARK:
  `identity<int>` och `identity<Holder>` får var sitt besked. Instanser som används i
  koden klassas automatiskt, och andra typer kan tvingas fram med en explicit
  instansiering (`template double twice<double>(double);`). Själva mönstret rapporteras
  inte,
- den, om den är en medlemsfunktion, antingen är `static` eller inte är virtuell och tillhör
  en värdetyp. `this` fungerar då som `const T&` (const-metod) eller `T&` (övriga).
  Konverteringsfunktioner stöds inte än. En egen destruktor (STN-68) granskas som en metod och
  hindrar inte en värdetyp; varje ställe där den körs implicit (lokal, värdeparameter,
  temporär, fält, implicit tilldelning, inuti std via STN-67) är ett anrop till den. Silver
  översätter inte destruktorer än. En konstruktor (STN-66) granskas som
  en metod som inte är const, efter initieringslistan: initierarna som lokala initierare
  (bas/delegering = anrop), referensfält bara bundna till en referensparameter, ingen
  initierare får använda `*this`, och alla fält ska vara initierade innan kroppen börjar
  (tilldelning först i kroppen stöds inte än). I Silver (SLV-39) blir konstruktorn
  `void C(struct T *self, ...)` med initieringslistan som tilldelningar; invarianten ska gälla
  efteråt men antas inte före; basinitierare med fält översätts inte än.
  Se `corpus/55_constructors.cpp`,
- returtypen är en värdetyp (eller `void`), eller `const T&` till en värdetyp där varje
  `return` är en väg in i `*this` (bara const-metoder) eller en `const&`-parameter, eventuellt
  `c ? a : b` av sådana (getters). Hos anroparen är resultatet ett lån av objektet/argumentet,
  som `v[i]`. I Silver returneras värdet. Se `corpus/18_ref_return.cpp`.
  Likaså `const T*`, en pekare (ett lånevärde, se nedan): varje `return` klarar
  uttrycksreglerna, och lånemodellen kontrollerar att det pekaren lånar överlever anropet.
  I Silver returneras `{_Bool has; T v;}` (bara `nullptr`, `get_if` och anrop av sådana
  funktioner översätts). Se `corpus/19_nullable_return.cpp`,
- varje parameter är en värdetyp, en `const T&` (läsparameter) eller en `T&` (in out, som i
  SPARK) till en värdetyp. `T&&`, pekare och icke-const referenser som returtyp underkänns,
- kroppen bara består av tillåtna konstruktioner:
  - satser: block, `return`, `if`, `for`, `while`, `do`, `switch`/`case`/`default`, `break`,
    `continue` och lokala variabler av värdetyp (inte `static`/`thread_local`).
    Structar måste initieras i sin deklaration, helt (`Vec2 v{}`, `Vec2 v = Vec2()`; inte
    `Vec2 v;`), och ingen variabel får läsas i sin egen initierare (`int x = x + 1`). En
    skalär (heltal, flyttal, `bool`, enum) får deklareras utan initierare (`int x;`) om den
    tilldelas med `=` före varje läsning på alla vägar. Det avgörs av clangs analys av
    oinitierade värden (samma som `-Wsometimes-uninitialized`): "may be read before it is
    assigned". En sådan variabel får aldrig bindas till en referens eller fångas i en
    lambda, eftersom clang räknar ett `T&`-argument som initiering
    (`corpus/07_init.cpp`),
  - uttryck: literaler, aritmetik/jämförelse/tilldelning, `?:` och säkra numeriska casts.
    Varje deluttryck måste ha värdetyp eller vara `void`,
  - sidoeffekter (`++`, `--`, `=`, `op=`, även struct-tilldelning) bara som egen sats,
    i for-loopens steg eller i en komma-kedja på satsnivå, aldrig inuti ett annat uttryck.
    Samma regel som SPARK, och den stänger ute osekvenserad UB som `i++ + i`
    (`corpus/06_side_effects.cpp`),
  - **lokala referenser** (`T&`/`const T&` till en värdetyp), bundna till
    - en väg (`const Ball& b = w.ball`): ett annat namn för vägen. Aliasing-kontrollen ser
      igenom referensen, så `move_by(b.pos, w.ball.pos)` underkänns,
    - en temporär (`const Vec2& v = Vec2{a, a}`): livslängden förlängs,
    - ett element (`Vec2& p = t.players[i]`), alltså ett anrop som returnerar en referens och
      har exakt ett referensargument eller objekt, som är en väg: ett **lån** av den vägen.
      Medan lånet lever underkänns varje ändring av behållaren eller av något som innehåller
      den, utom skrivningar genom referensen själv (`push_back`, ett andra icke-const
      `t.players[j]`, `reset_team(t)`, `t = other`). Lånet slutar vid sista användningen av
      referensen eller ett alias till den, enligt kontrollflödet (clangs liveness-analys, som
      Rusts NLL); en range-for lånar hela loopen.
      För betrodda `std::map` och `std::set` kan loopen läsa element via
      `const auto&` eller kopiera dem. För map stöds även `const auto& [key, value]`.
      Silver saknar en modell för de här behållarna.
    - **lånevärden** (STN-39): pekare, iteratorer, `std::span`/`string_view` och std-mallar
      som `std::pair<iterator, bool>`. De får vara lokala variabler, uttryck och kasserade
      resultat; vad de lånar följer lånemodellen (STN-62, `docs/stone_model.md`) enligt
      `summaries/std.txt`. Pekare: `&x`, `*p`, `p->f`, `==`/`!=`, ingen aritmetik.
      Se `corpus/52_loan_values.cpp`.

    Andra bindningar underkänns: `c ? a : b`, en referens som kan peka på flera objekt
    (`std::min(a, b)`), och en referens via ett temporärt argument (`id_ref(5)`, som dinglar).
    `T&&` underkänns (`corpus/13_local_refs.cpp`),
  - structar: fältåtkomst med `.` (och `this->` i const-metoder), aggregatinitiering
    (`Vec2{x, y}`, `Vec2 v{}`, även standardvärden för fält), triviala
    kopierings-, flytt- och standardkonstruktorer samt trivial tilldelning,
  - namn: parametrar, lokala variabler, enum-konstanter och `constexpr`-variabler
    (även `static constexpr`-medlemmar),
  - direkta anrop av fria funktioner, medlemsfunktioner, användardefinierade operatorer
    och konstruktorer. Kopior och tilldelningar av värdetyper som är medlemsvisa
    (triviala eller implicita) räknas inte som anrop,
  - **anti-aliasing** vid anrop: ett argument till en `T&`-parameter, eller objektet vid
    anrop av en icke-const-metod, måste vara en variabel eller ett fält (`a`, `p.x`,
    `this->x`). Det får inte överlappa ett annat referensargument eller tilldelningsmålet
    (`swap(a, a)`, `move_by(p, p)`, `move_x(p, p.x)`, `a.absorb(a)`, `a = f(a)`). Ett argument
    som är en temporär räknas inte, eftersom ändringen inte syns.
  - **anrop som ändrar sina argument** är sidoeffekter. De får bara stå som egen sats, som
    hela högerledet i en tilldelning, som hela initieraren, som hela returvärdet eller som
    hela villkoret i en `if` (som utvärderas en gång, före grenarna). Det motsvarar i stort
    SPARK 2023:s regler för funktioner med sidoeffekter
    (`corpus/11_inout.cpp`),
- och alla funktioner den anropar antingen är definierade i de analyserade källorna och
  själva är Stone, eller är **betrodda** (se nedan). Det beräknas som en största fixpunkt,
  så ömsesidig rekursion är tillåten.

**Princip: tillåtelselista.** Allt som inte uttryckligen är tillåtet i
`adapters/clang/LocalCheck.cpp` ger `not-stone: unsupported ...`. Ett felaktigt
nej är bara irriterande, men ett felaktigt ja förstör hela idén. Därför byggs
verktyget inte på lösa `clang-query`-skript: en felskriven matcher där ger 0
träffar utan felmeddelande.

Stone säger inget om overflow, division med noll eller terminering. Det hör till Silver.

## Bronze: `PORK_DEPENDS`

Ett flödeskrav står först i kroppen, bredvid `PORK_PRE`, och säger vad en
utdata får bero på:

```cpp
int invoice(int price, bool discount, int& audit) {
  PORK_DEPENDS(audit, price);            // revisionen visar det ursprungliga priset
  PORK_DEPENDS_RESULT(price, discount);  // returvärdet
  audit = price;
  return discount ? price / 2 : price;
}
```

Platserna är vanliga uttryck (parametrar, fältvägar, fält i `*this`), så
kompilatorn kontrollerar namnen. En plats täcker sina delar. En utdata som kan
behålla sitt värde beror på sitt ingångsvärde, som då måste stå med.
Kraven prövas mot det härledda flödet: håller de blir domen `bronze` (eller
`silver`), annars `stone: not bronze: audit depends on discount, which
PORK_DEPENDS does not allow`, och funktionen kan inte bli silver. Ett flöde
som inte räknas ut (`not computed`) uppfyller inget krav. Ett krav som inte
håller ger dessutom ett fel på stderr och utgångsstatus 1, som `pork::stone`,
också när funktionen inte blir Stone. En anropare som bygger på
eftervillkoret i en funktion vars krav inte håller blir inte silver.
Utan krav är allt som förut.

## Beroendeoraklet: `--flow-oracle`

Ett testorakel för `--flow` (dolt, kräver `--flow --silver`): för varje del av
en utdata i en Silver-funktion körs funktionen två gånger med lika värden i det
flödet säger att delen beror på och godtyckliga värden i resten. Bevisaren
visar att delen blir lika; ett motexempel är `flow oracle: HOLE`. Se
docs/bronze.md, steg 3.

## `throw`

`throw std::length_error("...");` som egen sats (STN-83) avslutar funktionen.
Bara typerna i `<stdexcept>` med en strängliteral; domen bär antagandet
`throws std::length_error`, som sprids till anroparna. Silver (SLV-43) ska
bevisa att kastet inte nås under förvillkoren, och en Silver-funktion bär inte
antagandet. I `--flow` räknas vägar som kastar inte (bara normal retur).
`throw` i uttryck, omkastning och `try`/`catch` avvisas.

## Flöde: `--flow` (Bronze steg 1, ingen dom)

För Stone-funktioner skriver pork vad varje utdata kan bero på och om den
skrivs på varje normal returväg ([docs/bronze.md](bronze.md)):

```
quote: stone
  flow: return <- discounted, price
  flow: audit <- price (always written)
audit_if: stone
  flow: audit <- audit, enabled, price (maybe written)
```

Ett namn till höger är ingångsvärdet (`audit` i `audit_if`: det gamla värdet
kan finnas kvar). Styrberoenden räknas med. Ett anrop använder den anropades
sammanfattning, också i en annan TU; betrodd kod utan definition är
konservativ (allt beror på allt, ingen säker skrivning). Konstruktioner där en
skrivning inte syns som en plats ger `not computed: <varför>`. Inga nivåer
påverkas.

## Lånemodellen: `--loans`

En generell modell för lån och alias bredvid reglerna ovan, utan en regel per idiom
(STN-62, [`docs/stone_model.md`](stone_model.md)). Modellen körs alltid och Stone kräver att
både reglerna och modellen godkänner (`--loans=enforce`, standard). `--loans=report` ger samma
dom och skriver dessutom båda domarna var för sig under varje funktion (`  loans: ok; local: ok`). Nya idiom kräver en sammanfattning, inte en ny regel. Egna funktioner i
samma TU sammanfattas ur kroppen (vad de ändrar, vad returen lånar), så `reset_team(t)` som
bara ändrar `t.wind` ogiltigförklarar inte referenser in i `t.players`.

Std-funktionernas effekter (det vi litar på utan bevis) står som data i
[`summaries/std.txt`](../summaries/std.txt), en regel per rad med paragrafen i standarden, till
exempel att `map::insert` inte ogiltigförklarar element och att `it = m.erase(it)` är tillåtet.
Filen byggs in i binären; `--summaries <fil>` lägger till eller ersätter regler (format i
`core/LoanSummaries.hpp`). En saknad regel ger den försiktiga standarden, aldrig ett ja.
`tests/check_summaries.py` (ctest `summaries_runtime`, `ctest -LE runtime` hoppar över) prövar
raderna mot körning: genererade program granskas av pork och körs med `-D_GLIBCXX_DEBUG` och
ASan; pork ok men körningen larmar är ett fel i en rad.
`tests/check_loans.py build/pork corpus --list` visar var reglerna och modellen är oense.

## Betrodda funktioner (antaganden)

Som i SPARK, där en specifikation kan vara i SPARK medan kroppen inte är det: pork litar
på funktionen utan att analysera den och redovisar det som ett **antagande**.

- `--trust fil`, med en post per rad och `#`-kommentarer tillåtna:
  - `std::fabs`, `hypot`, `std::clamp`: en funktion (kvalificerat namn utan templateargument;
    medlemmar i klasstemplates heter som mallen, t.ex. `std::optional::reset`),
  - `std::optional::*`: alla medlemmar i `std::optional` (konstruktorer, operatorer, ...),
  - `type std::optional`: betrodd värdetyp (se ovan).

  Funktionsposter gäller **bara funktioner utan definition** i de analyserade källorna. Har
  funktionen en definition analyseras den ändå.
- `PORK_TRUSTED` (`[[clang::annotate("pork::trusted")]]`, se `include/pork/annotations.hpp`)
  på en deklaration eller definition. En definition får beskedet `trusted`, och kroppen
  analyseras inte.

Anropare får beskedet `stone: assuming std::fabs, hypot, type std::optional`. Antagandena
följer med transitivt och skrivs ut sorterade, med trust-namnet (en post för
`std::clamp<float>` och `std::clamp<int>`).

En forwarding-referens (`U&&`) i en betrodd funktion från en systemheader räknas som en
läsning, inte som `T&`. Det är en del av att lita på funktionen och behövs för t.ex.
`std::optional<int> o = a;` och `value_or(0)`. Att lita på något är ett medvetet beslut: `std::hypot` kan till
exempel sätta `errno`, en global, och är bara ren under `-fno-math-errno`.

**Strängliteraler** får skickas direkt som argument till en `const char*`-parameter:
`print_line("klart")` (loggning, utskrift). Literalen är oföränderlig och lever hela
programmet. En Stone-funktion kan inte själv ha en pekarparameter, så mottagaren är alltid
betrodd. Pekaren tillåts bara där: inte i en variabel, i aritmetik, i en jämförelse eller i
`?:`. I Silver blir den en C-literal, och kontraktets parameter heter `const_char_ptr`:
`void print_line__const_char_ptr(const char *s)`. Se `corpus/32_string_literal.cpp`. Juliet
(CWE-190/369 `_01`, `printLine` och `abs`/`fabs`/`imaxabs` betrodda): `good` gick från 92
till 184 Silver, fortfarande utan falsklarm. Med `--silver-strict` faller 46 av dem på
Juliets `RAND32()`, som slår om med flit.

## Förvillkor: `PORK_PRE`

```cpp
#include <pork/annotations.hpp>

int half(int a) {
  PORK_PRE(a >= 0);   // som SPARK Pre
  return a / 2;
}
```

`PORK_PRE(villkor)` står först i kroppen (flera går bra). Villkoret är ett rent uttryck
enligt Stone-reglerna; anrop till Stone-funktioner går bra. Vid vanlig kompilering
försvinner det (villkoret typkontrolleras men körs inte). När pork analyserar definieras
`PORK_ANALYSIS`, och då blir det ett anrop till `pork::pre`. Ett `PORK_PRE` efter en
annan sats underkänns. Se `corpus/14_pre.cpp`. Det är tänkt att bytas mot C++26 `pre(...)`
när clang stöder kontrakt.

## `std::string`

Med `type std::basic_string`, `std::basic_string::*`, `type std::allocator`, `std::allocator::*`
och `std::operator==`/`std::operator+` i trust-filen är `std::string` en betrodd värdetyp. Silver
modellerar den som en `std::vector<char>` med samma gräns (`--vector-bound`): literaler,
`size`/`length`/`empty`, `[]`/`at`/`front`/`back`, `push_back`, `+=`, `+`, `==`, `!=`, `=`.
Se `corpus/38_silver_string.cpp`.

## Kontextreferenser

Ett fält `T&`/`const T&` till en värdetyp (`Context& game_`) är ett dolt referensargument till varje
metod. Stone: två sådana fält i samma klass får inte kunna peka på samma objekt, och i ett anrop får ett
referensargument som kan nå `T` inte stå tillsammans med ett objekt som har en dold `T&` (utom när båda
bara läser). Ett anrop som kan skriva genom en dold icke-const referens är en sidoeffekt, även för en
const-metod (försiktigt: pork ser inte om metoden faktiskt skriver). Silver: fältet är en pekare till ett
eget objekt med godtyckligt innehåll. Se `corpus/39_context_refs.cpp`.

## Anrop som når en port

Implementationen bakom en port kan hålla referenser till parametrarna, `*this` eller kontexten (satta i
en konstruktor) och anropa tillbaka. Ett anrop som når en port (dispatch, eller ett argument som är eller
innehåller en portreferens) är därför alltid en sidoeffekt, även en const-metod: `port.peek() + 1`
underkänns, `int v = port.peek();` går. Inget lån in i en behållare utanför funktionen får leva över
anropet (det kunde dingla). STN-54.

## Virtuella funktioner och portar

```cpp
struct IPort { virtual ~IPort() = default; virtual void send(int v) = 0; };
struct Relay {
  void relay(int v) { if (v > limit) port.send(v); }  // stone: assuming dispatch IPort::send
  IPort& port;
  int limit;
};
```

- En polymorf klass (eller en klass med baser) är en **objekttyp**, inte en värdetyp: den får
  vara `*this` och stå bakom en referens, men inte kopieras (den skulle skäras av).
- Ett **gränssnitt** är en polymorf klass utan fält. Ett fält `I&` till ett gränssnitt är en
  **port**: analysen ser inget i den. Ett fält `T&` till en värdetyp är en kontextreferens (nedan).
- Ett **dispatchande anrop** (virtuellt, inte `Base::f()`, inte final) är Stone bara om varje
  implementation i källorna är Stone (`dispatches to non-stone 'X::f'` annars). För
  implementationer utanför källorna blir det antagandet `dispatch I::f`.
- Ett referensargument till ett dispatchande anrop får inte kunna vara något en känd
  implementation når (STN-55, avgörs över alla TU:er).
- Se `corpus/36_virtual.cpp`, `corpus/40_port_alias/`.

## Gränssnittskontrakt (kontraktsfil)

Kontrakt för ett gränssnitt står i en egen C++-fil som analyseras med de andra (som SPARK:s
`Pre'Class`/`Post'Class`). Senare kan de flytta till C++26 `pre`/`post` på deklarationen; en metod
får inte ha båda.

```cpp
#include <pork/annotations.hpp>
#include "IClock.hpp"
#pragma clang diagnostic ignored "-Wreturn-type"
PORK_INTERFACE_CONTRACT int now(const IClock& self) { PORK_POST(int r, r >= 0 && r < 1000); }
PORK_INTERFACE_CONTRACT void advance(IClock& self, int& t) { PORK_PRE(t >= 0 && t < 100); PORK_POST(void, t < 200); }
```

- Ett dispatchande anrop kontrollerar förvillkoret och använder eftervillkoret, aldrig en
  implementations kod. Efter anropet har allt utanför funktionen som implementationen kan nå
  (`*this`, kontexten, referensparametrarna) godtyckliga värden. Utan kontrakt: `no contract for
  'I::f (interface contract)'`.
- Varje känd implementation ärver förvillkoret och bevisas uppfylla kontraktet (Liskov): `does not
  satisfy the contract of 'I::f': ...`. En anropare är Silver bara om alla kända implementationer är det.
- Se `corpus/41_silver_contracts.cpp`, `42_silver_liskov.cpp`, `43_contract_file.cpp`.

## `assert`

`assert(c)` från `<assert.h>`/`<cassert>` fungerar utan pork-annoteringar, även i C:

- **Stone:** villkoret ska vara ett rent uttryck (som i `PORK_PRE`). Resten av makrots
  expansion (`sizeof`, `__assert_fail`) är libc:s och granskas inte.
- **Först i kroppen** (före eller bland `PORK_PRE`/`PORK_POST`): ett förvillkor, som GNAT:s
  `pragma Precondition` först i en kropp. Antas i funktionen och kontrolleras vid varje
  anrop (`not silver: precondition of 'f': assert(c)`).
- **Längre ner:** en kontroll som ska bevisas, som SPARK:s `pragma Assert`
  (`not silver: assertion 'c' at fil:rad`).
- Med `NDEBUG` finns inget assert kvar; pork analyserar det bygge som compile_commands
  beskriver.
- Se `corpus/34_assert.cpp` och `corpus/35_assert_c.c` (samma fall i C).

## Eftervillkor och modulärt bevis: `PORK_POST`

```cpp
float length(Vec2 v) {
  PORK_PRE(is_vector(v));
  PORK_POST(float r, r >= 0.0F && r <= 2.0F * coord_limit);  // som SPARK Post
  return std::hypot(v.x, v.y);
}

void bump(int& x) {
  PORK_PRE(x < 100);
  PORK_POST(void, x <= 100);  // void-funktion: inget resultat
  x = x + 1;
}
```

- `PORK_POST(R r, villkor)` står först i kroppen, med `PORK_PRE`. `r` måste ha
  funktionens returtyp. Villkoret är ett rent uttryck över `r`, parametrarna och `*this`.
- En värde- eller `const&`-parameter i villkoret är *ingångsvärdet* (även om funktionen
  ändrar sin kopia), en `T&`-parameter och `*this` i en icke-const-metod *slutvärdet*.
- Kompileras bort i vanliga byggen (en lambda som aldrig anropas).
- **Silver i funktionen:** eftervillkoret kontrolleras vid varje `return` och i slutet av
  en void-funktion (`not silver: postcondition of 'f'`).
- **Silver hos anroparen (modulärt):** en funktion med `PORK_POST` tas inte med som kod.
  Anroparen kontrollerar förvillkoret och får sedan ett godtyckligt giltigt resultat som
  uppfyller eftervillkoret; `T&`-parametrar och `*this` får godtyckliga nya värden. Det
  anroparen behöver veta måste alltså stå i eftervillkoret. Funktioner utan `PORK_POST`
  tas med som kod som förut.
- En anropare är bara Silver om eftervillkoren den bygger på är bevisade, annars
  `not silver: relies on the unproven postcondition of 'f'`.
- Se `corpus/26_silver_post.cpp`.

## Typinvarianter: `PORK_INVARIANT`

```cpp
struct Range {
  int lo;
  int hi;
  PORK_INVARIANT(lo <= hi);  // som SPARK Type_Invariant
};
```

- Blir metoden `bool pork_invariant() const`, som också går att anropa i vanlig kod och tester.
- Silver lägger till invarianten vid funktionsgränser, efter typ:
  - förvillkor för en parameter av typen (värde, `const&` eller `&`) och för `*this`;
  - eftervillkor för en `T&`-parameter, `*this` i en icke-const-metod och ett returvärde av typen.
- Inuti en funktion får invarianten brytas tillfälligt; den gäller vid in- och utgång.
- Fel: `invariant of 'T' on entry to 'f'` (anroparen bröt den) och `on exit from 'f'`.
- Funktioner som invarianten själv anropar, transitivt, undantas (annars rekursion).
- Med `PORK_POST` antar anroparen invarianten för resultatet och för `T&`/`*this` efter anropet.
- Gäller inte element i en `std::vector<T>` eller fält av typen i en annan struct: den yttre
  typens invariant får uttrycka det.
- Se `corpus/27_silver_invariant.cpp`.

## Lösare: `--silver-solver`

cbmc använder normalt sin inbyggda SAT-lösare (minisat). `--silver-solver` väljer en annan:
`cadical` (eller en annan SAT-lösare som läser DIMACS, namn eller sökväg) eller en
SMT-lösare som cbmc känner: `z3`, `cvc5`, `bitwuzla`. Lösaren och dess version ingår i
cache-nyckeln. cbmc startas i en egen processgrupp (`setsid`) som dödas när cbmc avslutats,
så en lösare (egen process) blir aldrig kvar, inte heller när cbmc dör av minnesgränsen.

Mätt på rugby (eget, opublicerat spel, se [Mätning mot riktig kod](#mätning-mot-riktig-kod-rugby-srccorecpp); `--unwind 130`, summan av alla bevis, var lösare i egen tom cache):

| Lösare | Silver | Summa | Utan timeouts | Timeouts |
|---|---|---|---|---|
| minisat | 44 | 571 s | 210 s | 3 |
| **cadical** | 44 | **278 s** | **158 s** | 1 |
| cvc5 | 42 | 534 s | 174 s | 3 (+ ett SMT-fel) |
| z3 | – | korpusen 25 × långsammare | | |
| bitwuzla | – | SMT-LIB-frågan för structar med flyttal avvisades i testet med cbmc 6.11 och bitwuzla 0.10 (se nedan) | | |

minisat, cadical, cvc5 och z3 ger samma domar på korpusen (z3 utom en timeout). Alt-Ergo går
inte: cbmc:s flyttal i SMT-LIB använder `concat` med fler än två argument, som Alt-Ergo inte
läser (heltal fungerar). Bitwuzla (0.10) klarar flyttal, och skalära flyttal via cbmc fungerar,
men i vår körning med cbmc 6.11 och bitwuzla 0.10 innehöll SMT-LIB-frågan för en struct med
flyttal `concat` direkt på `FloatingPoint`-termer, och lösaren avvisade frågan. Varje egenskap
i en funktion med flyttal i structar fick då status ERROR. `cadical` rekommenderas när den
finns installerad.

cbmc:s nya inkrementella SMT2-läge (`--incremental-smt2-solver 'bitwuzla --lang smt2'`, märkt
experimentellt i 6.11) löser inte flyttalen heller. Heltal fungerar (`wrap` i korpusen
bevisas), men för `vadd` (flyttal i en struct) växer cbmc själv med ~1,5 GB/s i steget
"converting SSA", medan bitwuzla stannar på 13 MB. Därför stöder pork inte läget. Kör inte
heller cbmc direkt utan minnestak: systemd-oomd dödar då hela terminalen och inte bara cbmc.
Använd t.ex. `systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 timeout 60 cbmc ...`.

Bara status SUCCESS räknas som bevisat. ERROR/UNKNOWN utan något FAILURE ger "prover failed";
cbmc lämnar egenskaper UNKNOWN när den redan hittat ett motexempel, det är normalt. (Före
cache-format 2 räknades ERROR som bevisat; gamla cachefiler läses därför inte.)

## Omprövning av motexempel i program med unioner

Testramen (`pork_harness`) får godtyckliga indata genom att deklarera dem
oinitierade. När en unionmedlem i sådana indata kopieras kan cbmc 6.11.0 ge
ett motexempel som inte stämmer med antagandena (`--no-simplify` och ESBMC
godkänner samma program). Ges indata i stället av en funktion utan kropp
(`T x = nondet_T();`) godkänner cbmc programmet. Att läsa en oinitierad lokal
variabel är odefinierat beteende i C (C11 6.3.2.1p2), så felet ligger i hur
testramen skapar indata, inte nödvändigtvis i cbmc. Det hittades av
beroendeoraklet, som rapporterade ett hål i rugbys `player_of` (`std::visit`
över en `std::variant`, i C en union). Att skapa indata med funktioner utan
kropp är planerat; kontrollen nedan är en tillfällig lösning.

Tills vidare kör cbmc allt som förut, men hittar cbmc fel i ett program med en
union körs samma program med ESBMC (eller cbmc `--no-simplify` om ESBMC saknas).
Godkänner den programmet gäller det; annars står cbmc:s svar kvar. Bevisarfel
och tidsgränser kontrolleras inte. Avbryts kontrollen (tidsgräns, körningsfel)
blir svaret ett bevisarfel med cbmc:s fynd i texten, inte ett slutresultat:
cachen sparar det som en timeout (prövas igen med längre `--silver-timeout`
eller `--silver-slow`) eller inte alls. Med `--keep-c` hamnar kontrollens
program i underkatalogen `union-check/`. Det skyddar mot falska motexempel, inte mot
ett felaktigt bevis (prov åt det hållet har inte hittat något).
`core/UnionCheckProver`; dold flagga `--silver-unions=auto|esbmc|no-simplify|cbmc`
(`cbmc`: ingen kontroll).

Mätt på rugby (tom cache, `--flow-oracle`, 2 jobb):

| Läge | Tid | Silver | Falska hål | Kontrollerade / godkända |
|---|---|---|---|---|
| cbmc, ingen kontroll | 586 s | 50 | 1 | – |
| kontroll med ESBMC | 594 s | 50 | 0 | 11 / 1 |
| kontroll med cbmc `--no-simplify` | 600 s | 50 | 0 | 11 / 1 |
| alla unionprogram med ESBMC | 499 s | 49 | 0 | (50 program) |
| alla unionprogram med `--no-simplify` | 673 s | 49 | 0 | (50 program) |

Nästan alla rugbyprogram innehåller en union (typdefinitionerna följer med
även när funktionen inte använder varianten), så att skicka alla unionprogram
till en annan bevisare kostar två Silver-domar (minnet tar slut). Kontrollen
av fel kostar 1–2 %.

## Bevisare: `--silver-prover=esbmc`

Silver kan också köras med [ESBMC](https://github.com/esbmc/esbmc) (8.6, byggd från källkod
mot LLVM 22 och bitwuzla).
Adaptern `adapters/esbmc/EsbmcProver` får samma C-program och kontrakt som cbmc. En liten
header (`--include-file`) mappar `__CPROVER_assert/assume/isnan/isinf` till ESBMC:s
motsvarigheter. Standardlösaren är bitwuzla, inlänkad i ESBMC; `--silver-solver z3` m.fl.
går om ESBMC är byggd med dem. Resultattabellen i textutdatan läses (`--multi-property`).

- **En funktion utan kropp är alltid ett fel.** ESBMC varnar bara och antar ett godtyckligt
  värde. Utan mappningen av `__CPROVER_assert` sa ESBMC därför SUCCESSFUL fast pork:s
  kontroller aldrig kördes. Nu blir varningen `no contract for ...`, som hos cbmc.
- Beskrivningarna översätts till pork:s (cbmc:s) ordalydelse, och pork:s egna kontraktsfel
  (`precondition of` m.fl.) visas före ESBMC:s egna kontroller på samma rad.
- ESBMC saknar egen kontroll för flyttalsdivision med noll; samma fel hittas som NaN eller
  överspill. Korpusen har därför `// expect[esbmc]: ...` för det fallet.
- Korpusen körs med båda (`corpus` och `corpus_esbmc`). Domarna är desamma.

Mätt på rugby (tom cache, `--unwind 130`, 4 jobb à 4 GB, 120 s):

| Bevisare | Silver | Tid | Skillnad |
|---|---|---|---|
| **cbmc + cadical** | **46** | **142 s** | |
| ESBMC + bitwuzla | 43 | 189 s | `chase_point` (och därmed `move_defender`) timeout, `check_tackle` slut på minne |

Ingen motsägelse mellan bevisarna. I den här mätningen gav cbmc + cadical 46 Silver-resultat och ESBMC + bitwuzla 43. Bitwuzla gör stor nytta på
flyttal i structar (`vadd` på under en sekund, där cbmc med bitwuzla inte fungerar), men i
rugby går tiden åt till att rulla ut loopar, inte till lösaren. Funktionerna som dör hos cbmc
(`solve_pass`, `apply` som anropar den) dör hos ESBMC också: loopen i `solve_pass` rullas ut
~2 600 gånger. cbmc med cadical är därför fortsatt det rekommenderade valet (porks standardlösare är minisat, inbyggd i cbmc; välj cadical med `--silver-solver cadical`).

## Självtest av förvillkoren

Förvillkor som aldrig kan gälla samtidigt (`PORK_PRE(x > 5 && x < 3)`, två `PORK_PRE` som
motsäger varandra, `f != f` när flyttal antas ändliga, en motsägelsefull `PORK_INVARIANT`)
gör varje bevis tomt: allt följer ur något falskt. Silver prövar därför i ett eget litet
program att förvillkoren och invarianterna kan uppfyllas av minst en indata, annars
`not silver: preconditions can never hold`. Eget program, eftersom samma kontroll i
beviset gjorde cbmc mångdubbelt långsammare. Se `corpus/28_silver_vacuity.cpp`.

## Strikt Silver: `--silver-strict`

Silver betyder "inget odefinierat beteende". Omslag i `unsigned`-aritmetik och
heltalskonverteringar som tappar värdet (`char r = c + 1`, `(short)x`, negativt `int` →
`unsigned`) är definierade i C++, men ger nästan alltid fel värde (CWE-190, CWE-197).
`--silver-strict` underkänner även dem, som SPARK, som bevisar intervallet vid varje
konvertering:

- **Omslag:** bevisarens egen kontroll (`--unsigned-overflow-check` i både cbmc och esbmc).
  ESBMC säger inte om operanden var signed eller unsigned, så i strikt läge heter fyndet
  `arithmetic overflow on integer +` med esbmc.
- **Konverteringar:** pork lägger själv in en kontroll i C-översättningen
  (`pork_<från>_to_<till>_checked`) när måltypen inte rymmer alla värden av källtypen,
  både implicit och explicit. Den görs i översättningen eftersom ESBMC saknar cbmc:s
  `--conversion-check`. Då betyder kontrollen samma sak för båda bevisarna.
  Fynd: `integer conversion out of range`.

Avsiktligt omslag (hashfunktioner, slumptal) blir alltså inte Silver i strikt läge. Juliets
`RAND32()` (`-URAND31() - 1`) underkänns till exempel innan det riktiga felet hittas. Se
`corpus/31_silver_strict.cpp`. Juliet (CWE-190/369 `_01`): alla 22 `bad` som var Silver utan
flaggan underkänns, och de 92 `good` är fortfarande Silver.
Rugby (snabbt pass, `--silver-timeout 10`): 43 Silver med och utan flaggan, inga nya fynd.
Kärnan räknar mest med flyttal, och heltalen (fas, poäng, index) har redan förvillkor eller
mättning. Strikt läge kostar ingen märkbar tid där.

## Silver (`--silver`)

Silver betyder frånvaro av körtidsfel för alla indata som uppfyller förvillkoren. Pork
översätter varje Stone-funktion till C och låter [CBMC](https://github.com/diffblue/cbmc)
bevisa det. Clang löser upp typer, överlagringar, mallinstanser och implicita konverteringar,
och samma C-modell, med samma kontrakt och gränser, kan då skickas till både CBMC och ESBMC.

- **Kontroller:** signed overflow, division med noll (heltal och flyttal), odefinierade
  skift, flyttals-overflow (±inf) och NaN, flyttal → heltal som inte ryms, förvillkor vid
  varje anrop, och att loopar tar slut inom `--unwind` varv (standard 64). En loop över en vektor
  (range-for eller `i < v.size()` i `size_t`) får villkoret `i < N` tillagt (N =
  `--vector-bound`). Det följer av storleksgränsen, men cbmc ser då att loopen slutar efter N
  varv i stället för att rulla ut den till `--unwind`. Det gjorde rugbys loopar över spelarna
  ungefär 4 gånger billigare.
- **Indata:** godtyckliga, men flyttal antas ändliga och enums antas ha ett uppräknat värde.
  `PORK_PRE` antas i funktionen som prövas och kontrolleras (assert) när den anropas.
- **Anrop:** allt som anropas tas med (transitivt), så ett fel i en anropad funktion är ett
  fel även hos anroparen. Betrodda funktioner går till ett kontrakt i C:
  `contracts/std.c` (`std::hypot`, `std::fabs`, `std::abs`, `std::sqrt`, `std::clamp`,
  `std::min`, `std::max`) eller egna filer med `--contracts fil.c`. Kontraktets namn är
  det kvalificerade namnet med `::` → `__` och parametertyperna utan `const`/`&`:
  `std::hypot(float, float)` → `std__hypot__float__float`. Ett kontrakt kan ha förvillkor
  (`std::clamp`: `!(hi < lo)`). Saknas kontrakt: `not silver: no contract for 'f'`.
- **Utdata:** `silver[: assuming ...]` eller `stone: not silver: <fynd> at fil:rad[; assuming ...]`.
- `--silver-counterexample` visar skalära ingångsvärden för det första rapporterade
  fyndet från CBMC, med parameternamnen från C++. Värdena hämtas ur felspåret och gäller
  Silvers modell, inklusive eventuella kontrakt och antaganden. Objektparametrar,
  mottagaren `this` och värden som bevisaren väljer inuti kontrakt visas inte.
- `--keep-c <katalog>` sparar de genererade C-programmen, `--silver-timeout` sätter
  tidsgränsen per funktion (standard 120 s).
- Funktionerna prövas parallellt: `--silver-jobs N` (standard: antalet kärnor, men högst
  3/4 av RAM delat med `--silver-memory`). `--silver-memory` är minnesgränsen per
  cbmc-process i MB (standard 4096, 0 = ingen); utan den kan några stora bevis ta allt
  minne. Besluten beror inte på antalet jobb. Rugby: ~3 min mot >10 min sekventiellt.
- Svaren sparas i `~/.cache/pork/silver` (`--silver-cache <katalog>`, `$XDG_CACHE_HOME`
  respekteras). Nyckeln är SHA-256 av hela C-programmet (funktionen, allt den anropar,
  typer, kontrakt, `#line`) och bevisarens konfiguration (cbmc-version, flaggor,
  `--unwind`, minnesgräns, kontraktsfilernas innehåll). Bara det som ändrats bevisas
  igen; flyttas en funktion i filen ändras dess `#line` och den bevisas om.
  Tidsgränsen ingår inte i nyckeln: ett svar (bevisat eller fynd) gäller oavsett gräns.
  En timeout sparas med sin gräns och återanvänds bara när den nya gränsen inte är större,
  så en högre `--silver-timeout` prövar bara de som inte hann klart. `--silver-slow`
  prövar sparade timeouts igen även med samma gräns. `--no-silver-cache` stänger av
  cachen (korpusen använder det). Rugby: 3 s med varm cache.
- **Snabbt pass, sedan långsamt pass.** Vardagskörningen med kort tidsgräns, de tunga
  bevisen separat (natt, CI). pork skriver hur många som slog i gränsen:

  ```
  pork --silver --silver-timeout=10 ...    # rugby: 35 s, 40 Silver, 8 timeouts
  pork --silver --silver-timeout=300 ...   # bara de 8: 161 s, 46 Silver
  pork --silver --silver-timeout=10 ...    # efteråt: 3 s, 46 Silver (allt från cachen)
  ```

  Ett fel som inte är en timeout (t.ex. slut på minne) sparas inte. En äldre timeout med
  lägre gräns ligger då kvar och visas i nästa snabba pass (`solve_pass` i rugby: timeout
  vid 10 s, slut på minne vid 300 s).

Översättningen klarar Stones delmängd, `std::optional`, `std::vector` och `std::variant`,
men inte andra betrodda typer (`std::string`). De ger `cannot translate to C: ...`.
`std::vector<T>` modelleras med högst `--vector-bound` element (standard 16); ett bevis
gäller bara så stora vektorer och redovisas som `assuming std::vector size <= 16`.
Hur översättningen går till, och ESBMC som alternativ till CBMC: [docs/silver.md](silver.md).

## Krav (opt-in)

`PORK_STONE` (`[[clang::annotate("pork::stone")]]`) på en funktion betyder att den **måste**
bli Stone; antaganden är tillåtna. Annars skriver pork ett kompilatorlikt fel på stderr
och avslutar med kod 1:

```
a.cpp:15:40: error: 'required_bad' is marked pork::stone but is not-stone: uses variable 'g' with static storage
```

Det fungerar på en deklaration i en header (gäller definitionen) och på en template
(gäller varje instans). Det motsvarar `SPARK_Mode`: skydd mot att något försämras i CI.

Exit-koder: 0 = inga brutna krav, 1 = minst ett brutet krav, 2 = analysen kunde inte köras
(kompileringsfel, trust-fil saknas).

### Kända luckor/val

- Lambdors `operator()` rapporteras inte som egna funktioner (RecursiveASTVisitor besöker
  dem inte som deklarationer). Funktionen som innehåller lambdan underkänns ändå.
- En template som aldrig instansieras ger inget besked alls. Pork påstår inget om den.
- Funktioner i systemheaders (`std::clamp<float>`) analyseras aldrig. De måste vara betrodda.
- Lån är konservativa: ett andra icke-const-element (`Vec2& q = t.players[j]` medan `p`
  lever) underkänns även när `i != j`.
- Accessorer (metod i en systemheader som returnerar `T&`, eller `operator->`, och är const
  eller har en const-tvilling): `v[i]`, `*o`, `o.value()`, `o->x`. Läses resultatet räknas
  anropet som const. Skrivs det (`v[i].f = x`, `*o = 1`, `o->f = x`, T&-argument) är det en
  skrivning till behållaren: den måste vara en väg, och levande lån in i den underkänner.
  Se `corpus/15_access.cpp`.
- `std::vector<bool>`: `v[i]` ger en proxy (`std::_Bit_reference`), inte `bool&`. Den räknas
  som elementet i två former: läsning (proxyn blir `bool`) och skrivning `v[i] = <bool>`.
  Allt annat med proxyn (spara den, `flip()`, `v[i] = v[j]`, tilldelning inuti ett uttryck)
  ger not-stone. I Silver blir båda formerna elementet i modellen. Se
  `adapters/clang/VectorBool.hpp`.
- `std::span<const T>` (dynamisk storlek) som parameter: en läsvy. Andra spans (lokala,
  skapade vid anrop, `first`/`subspan`) är lånevärden som lånemodellen följer, även
  aliasing mot ett ändrat argument. I Silver bara som parameter, skapad från en
  `std::vector<T>` eller en annan span-parameter, med `s[i]`, `size`, `empty`, `front`,
  `back` och range-for. Trust-filen behöver `std::span::*` (inte `type std::span`:
  en span är ingen värdetyp). I Silver: `{size, const T* data}`, från en vektor pekar `data`
  in i vektorns array; i harnessen en egen array med N element. Se `corpus/17_span.cpp`,
  `corpus/29_silver_span.cpp` och `adapters/clang/Span.hpp`.
- `(void)f(x)` som sats är samma som `f(x)`.
- Range-for bara över en `std::vector` som är en väg. Behållaren är lånad under hela
  loopen (ingen `push_back` i den); `T&`-variabeln är ett element. I Silver en indexloop.
- Implicita standardkonstruktorer i egen kod (`World w;`): fältinitierarna granskas. Ett
  fält utan initierare (och som inte själv har en sådan konstruktor) underkänns.
- `std::variant` (betrodd typ): `index()`, `holds_alternative`, `get`, konstruktion och
  tilldelning. `T* x = std::get_if<A>(&v)` är ett lånevärde som kan vara null (lånemodellen
  följer det, även om x flyttas till ett annat objekt). Se `corpus/16_variant.cpp`.
- Lambdor (lokala och i `std::visit`): kroppen granskas som en egen funktion; fångster med
  referens är extra argument vid anropet (`T&` om kroppen skriver till dem, annars `const T&`).
  Inte `this`, init-fångster, `mutable` eller lambdor som kopieras/skickas vidare.
- Anrop till egna funktioner definierade i en annan TU: Silver tar med C-översättningen
  därifrån (samma C-namn, från USR), annars kontraktet.
- Betrodda typers typargument kontrolleras alla, även standardargument. `std::vector<T>`
  kräver därför också `type std::allocator`.
- Kod med kompileringsfel ger inga besked alls (exit ≠ 0).
- Silver: betrodda typer utom `std::optional` och `std::vector` översätts inte än, och
  vektorer bevisas bara upp till `--vector-bound` element. Heltals-narrowing (`int` → `short`)
  och `unsigned`-omslag kontrolleras bara med `--silver-strict`, eftersom de är definierade i C++. Rekursion och loopar bevisas bara upp till `--unwind`.
  CBMC:s meddelanden visar uttrycken som C (`self->n + 1`).
- Ett standardargument utvärderas hos den som anropar. `f(int b = g)` kan alltså vara Stone
  medan anroparen `f(1)` inte är det (se `corpus/02_calls.cpp`).

## Struktur (hexagonal)

```
core/                kärna, inga clang-beroenden
  Facts.hpp          FunctionFacts: lokalt besked + direkta anrop (usr -> namn) + C-översättning
  Ports.hpp          IFactsSource (in), IReport (ut), IProver (ut)
  Classify.*         fixpunkten över anropsgrafen
  Silver.*           sätter ihop C-programmet per Stone-funktion och frågar bevisaren
  Analyse.*          användningsfallet: hämta -> klassa -> (silver) -> rapportera sorterat -> kravbrott
adapters/clang/      inkommande adapter: LibTooling, LocalCheck (tillåtelselistan), CTranslate (C för CBMC)
adapters/cbmc/       bevisare: kör cbmc som process, läser --json-ui
adapters/esbmc/      bevisare: kör esbmc som process på samma C, läser resultattabellen
adapters/process/    gemensamt för bevisarna: setsid, tidsgräns, minnesgräns, temporärfiler
contracts/std.c      kontrakt för betrodda standardfunktioner
adapters/text/       textrapport (utgående) och trust-fil (inläsning)
include/pork/        annotations.hpp: PORK_STONE / PORK_TRUSTED / PORK_PRE för kod som analyseras
app/main.cpp         kompositionsrot
tests/unit/          GoogleTest för kärnan med fejkar (ingen clang)
tests/check_corpus.py + corpus/   acceptanstest (svart låda)
```

## Korpusen = specen

Regelkatalogen står i `spec/1_stone.txt` (STN-n) och `spec/2_silver.txt` (SLV-n), i EARS med
stabila ID:n. Korpusfallen märks med `// rules: STN-n, ...`; se `corpus/README.md` för
strukturen och täckningsrapporten.

Varje funktion i `corpus/` har en kommentar på raden ovanför:

```cpp
// expect: stone                       // inga antaganden tillåts
// expect: stone: assuming <delsträng>
// expect: trusted
// expect: not-stone: <delsträng av orsaken>
// expect-violation                    // pork::stone-krav som bryts (exit 1)
// expect: silver[: assuming <...>]    // med `// pork-args: --silver` i filen
// expect: stone: not silver: <delsträng>
// expect[esbmc]: ...                  // ersätter raden ovan med --silver-prover=esbmc
```

En trust-fil för ett fall heter `<fall>.trust` (eller `trust.txt` i en katalog).

Flera besked på samma rad (template-instanser) anges med staplade förväntningar som
namnger funktionen:

```cpp
// expect: identity<int>: stone
// expect: identity<Holder>: not-stone: <orsak>
template <typename T> T identity(T x) { return x; }
```

En fil är ett fall, och en katalog är ett fall med flera översättningsenheter. Testet
fallerar om en funktion saknar förväntning, om en förväntning saknar utdata eller om
beskedet eller orsaken skiljer sig. Nya regler börjar som nya korpusrader.

## Mätning mot vanlig C++ (2026-11-22)

Repon som inte är skrivna för pork, utan omskrivning. Grunda kloner,
`compile_commands.json` från `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clang), alla
översättningsenheter på en gång, rugbys trust-fil (std-behållare, inte `std::string`). Bara
funktioner i reponas egna filer räknas (även mallinstanser i deras headers).

| Repo | Stil | Stone | Efter STN-63–65 | + konstruktorer (STN-66) |
|---|---|---|---|---|
| tinyxml2 (8224e42) | gammal C++, pekare överallt | 1 / 444 | 1 / 444 | 1 / 444 |
| fmt (14ac0d5) | modern C++, tunga mallar | 122 / 1144 | 142 / 1144 | 169 / 1144 |
| leveldb (2946558) | Google-stil, objekt och `Status` | 16 / 902 | 16 / 902 | 21 / 902 |
| yaml-cpp (f473f3b) | klasser, `std::string`, rekursiva typer | Pork kraschade | 180 / 1096 | 200 / 1096 |
| `projects/state` | hexagonal, portar som referenser | 6 / 75 | 6 / 75 (32 med `std::string` betrodd) | 7 / 75 (39) |

Senare samma dag: destruktorer (STN-68), egen kod inuti std (STN-67), rekursiva typer
koinduktivt (STN-63) och `std::unique_ptr` (STN-69/70) gav fmt 169, leveldb 45, yaml-cpp 210,
tinyxml2 4 med samma trust-fil; med `std::string` och `unique_ptr` betrodda fmt 170, leveldb 54,
yaml-cpp 230. Det som är kvar är pekar- och referensfält, polymorfa fält (`std::ostream`) och
returtyper som inte är värden.

Ommätt efter den adversariella granskningen (samma objekt som måste-fråga,
baser i implicita konstruktorer, forwarding-argument, G1 ur CFG:n, STN-82) och STN-72–81:
tinyxml2 4, fmt 175 (+6: en konverteringsfunktion, en klass med baser, `fmt::file` med egen
flyttkonstruktor), leveldb 45, yaml-cpp 210, rugby 58. Ingen funktion förlorade Stone, och de
striktare reglerna (samma objekt, upprepade baser, `value_or`) och G1-kontrollen ger inte ett
enda not-stone i repona: där finns inga nästlade behållare med iteratorpar. Största orsakerna
är oförändrade: pekar- och referensfält (219–370 per repo), returtyper, `std::string`-fält.

Konstruktorerna var första orsaken i alla repon, men gav bara +5 till +27: själva
konstruktorerna går nu igenom (delreglerna i STN-66 stoppar 2 i fmt), och det som stoppar dem
och deras anropare är klassernas fält. Rugby oförändrat 58 / 76.

Att lita på `std::string` ändrar nästan inget i de externa repona (+1 till +5). Första
orsaken till not-stone är i alla repon densamma:

- konstruktorer (100–150 per repo),
- pekar- och referensfält (`XMLDocument*`, `buffer<char>*`, `std::istream&`),
- klasser med egen destruktor/kopiering eller `std::unique_ptr` (leveldb 183),
- returtyper som inte är värdetyper (`basic_appender`/`char*` i fmt, `Status` i leveldb,
  `Emitter&` i yaml-cpp).

Stone v1 är alltså värdesemantik, och vanlig C++ bygger på objekt som äger resurser. Det
täpps inte till med idiomregler; det är en designfråga (ägande objekt, konstruktorer,
"Stone relativt portarna"). Mätningen hittade också en krasch i Pork (rekursiva typer, STN-63) och
två luckor i tillåtelselistan (STN-64 `static_assert`, STN-65 mallparametrar). yaml-cpps
`RegEx` (`std::vector<RegEx>`, 54 funktioner) skulle bli Stone om rekursiva värdetyper
godtogs (koinduktivt) i stället för att avvisas.

## Mätning mot riktig kod: rugby (`src/core/*.cpp`)

rugby är ett eget 2D-rugbyspel i C++23 med en deterministisk kärna. Det används
som mätobjekt i den här README:n och i dokumentationen, men koden är inte
publicerad. Rader märkta "branchen" mättes på en gren av rugby där koden
skrevs om med hjälp av porks återkoppling.

Mätt 2026-11-22 medan pork byggdes, i stort sett nyast överst.

| Version | Stone | Största hinder |
|---|---|---|
| + `const T*` som returvärde (branchen, lucka 1, cadical, `--unwind 130`, `--silver-timeout 60`) | **58 / 76, varav 52 Silver** | `planned_pass_from`, `planned_ball_holder` och `in_planned_pass_chain` är Stone och Silver. Kvar: `std::string`/`string_view`, iteratorer (`find_if`), `std::function`, konstruktorer; i Silver loopen i `solve_pass` |
| + lån till sista användningen, sidoeffekt som `if`-villkor, `const T&`-getters (branchen, luckorna 3–5) | **55 / 76** | `TurnController::planned` är Stone. Rugbys ursprungliga `move_players` (före omskrivningen där ingen referens in i `world` lever över `move_defender`) är Stone och Silver (med `--silver-timeout 300`). Kvar: `std::string`/`string_view`, pekare som returvärde (`planned_pass_from`), iteratorer (`find_if`), `std::function` |
| + `std::span<const T>`-parametrar och `move_players` utan lån över `move_defender` (branchen, cadical, `--unwind 130`, 4 jobb à 4 GB) | **54 / 76, varav 49 Silver**; 99 s med cache | `move_players` Silver; `step`, `TurnController::advance` och `preview_turn` nu Stone. Kvar i Silver: `step`, `advance` och `solve_pass` (slut på minne), `apply` (obevisat kontrakt för `solve_pass`), `preview_turn` (`std::vector::resize` översätts inte) |
| + `std::vector::assign` i Silver (branchen, cadical, `--unwind 130`) | **50 / 76, varav 48 Silver** | `TurnController::commit` är Silver. Kvar i Silver: `solve_pass` (slut på minne) och `apply` (PassTo, bygger på det obevisade kontraktet för `solve_pass`) |
| + `resume_play` mättar `phase` vid `max_phase` (branchen, cadical, `--unwind 130`) | **50 / 76, varav 47 Silver** | `resume_play` är Silver (det riktiga fyndet `phase + 1` rättat). Kvar i Silver: `solve_pass` (slut på minne), `apply` (PassTo, bygger på det obevisade kontraktet för `solve_pass`), `commit` (`std::vector::assign` översätts inte) |
| + kontrakt på `solve_pass`, fartgräns i `apply` (branchen, cadical, `--unwind 130`, 4 jobb à 4 GB) | **50 / 76, varav 46 Silver**; 133 s med kall cache, inga timeouts | `apply` (PassTo) dör inte längre av minnesgränsen: bevisad mot kontraktet, men `solve_pass` själv är obevisad (slut på minne), så `apply` får "relies on the unproven postcondition". Beviset hittade ett kantfall: nästan stillastående passare (t.ex. `top_speed` 1e-30) gav enorm `run_on / length` (rättat med tröskel 0,1 m/s). `chase_point` ligger nära tidsgränsen (123 s ensam) och kan växla mellan körningar |
| + `PORK_POST`/`PORK_INVARIANT` i rugby (branchen, cadical, `--unwind 130`, 4 jobb à 4 GB) | **50 / 76, varav 46 Silver**; 142 s med kall cache, inga timeouts | Silver: `solve_pass` och `apply` dör av minnesgränsen på 4 GB (`solve_pass` med 8 GB: timeout efter 120 s; `apply` inte omprövad), `commit` (`std::vector::assign` översätts inte), `resume_play` (riktigt fynd: `phase + 1` kan svämma över). `chase_point`/`move_defender` (tidigare timeout) är nu Silver tack vare eftervillkoren. Stone: `std::function`, pekare (`const PassTo*`), `vector<bool>`, konstruktorer, `const vector&` som returtyp, sidoeffekt i uttryck (`run_towards` i `move_players`) |
| + `std::variant`, `get_if`, lambdor/`std::visit` (branchen) | **44 / 70, varav 33 Silver** | Silver: CBMC-timeout (`solve_pass`, `chase_point`, `move_defender`), riktiga fynd (`ticks_elapsed`, `phase + 1`, division med noll i `move_ball` om `speed <= 0`). Stone: `std::string`/`string_view`, `std::span`, pekare som returvärde, `vector<bool>`, `ranges::find_if`, `std::function` |
| + accessorer, `o->x`, range-for, `(void)`, `T x;`, anrop mellan TU:er (branchen `silver-preconditions`) | **33 / 70, varav 28 Silver** | Silver: riktiga fynd (`phase + 1` kan svämma över, index i `check_tackle`, förvillkor till `Pitch::clamp`/`distance`, CBMC-timeout i `chase_point`). Stone: `std::variant`/`visit`, `std::string`/`string_view`, `std::span`, `TurnController` (vector av variant), lambdor, pekare (`const PassTo*`) |
| + `std::vector` i Silver | 20 / 65, varav 5 Silver; med förslaget på `PORK_PRE` i en kopia **25 / 25 Silver** (inkl. rättning av `velocity_of`) | rugby saknar förvillkor; Stone-hindren (65 funktioner) är kvar |
| + `std::optional` i Silver, läsning via icke-const accessor | 20 / 65, varav 4 Silver | utan förvillkor kan Vec2-aritmetiken svämma över |
| + förslag på `PORK_PRE` i en kopia av rugby | 25 / 68, varav 22 Silver (23 med rättning av `velocity_of`) | `std::vector` (`valid`, `check_try`); `velocity_of` hittade riktigt fel: subnormalt avstånd ger enorm hastighet |
| + Silver v1, utan `PORK_PRE` i rugby | 20 / 65, varav 3 Silver | Vec2-aritmetik kan svämma över för godtyckliga flyttal (rugby saknar förvillkor); `std::optional`/`std::vector` översätts inte än |
| v0 | 0 / 63 | struct som retur/parameter, medlemsfunktioner |
| + `T&`, betrodda typer: trust `std::optional` + `std::optional::*` | 18 / 65 | `Ball::is_loose`, `run_towards`, `predict_position`, `velocity_of` |
| + lokala referenser och lån | 20 / 65 | referenserna är inte längre hindret; nästa: icke-const `operator*`/`operator[]` i uttryck, `opt->x`, range-for, `(void)x`, `std::string`/`variant` |
| + trust `std::vector`, `std::allocator` | 19 / 65 | lokala referenser (`const Ball& ball = ...`), `std::string`/`string_view`, `std::variant`, `std::span`, icke-const `operator[]` i uttryck |
| + trust-fil `std::fabs`, `std::hypot`, `std::clamp` | 14 / 65 | `length`, `distance`, `support_spot`, `Pitch::clamp` blev `stone: assuming ...` |
| v1 + sidoeffekter, initiering, templates | 10 / 65 | oförändrat; `parse_number` blev 3 instanser |
| v1 | 10 / 63 | `std::`-typer (`string`, `optional`, `vector`, `variant`) i fält, parametrar och returtyper; `World&`-parametrar som muterar; anrop till libm (`std::hypot`, `std::fabs`) |

Bland de godkända i v1 finns `Vec2`-operatorerna, `Pitch`-metoderna, `is_forward` och
`in_opponents_in_goal`. `distance` underkänns bara för att `length` anropar `std::hypot`.

Möjliga nästa steg:

1. **Betrodda biblioteksfunktioner** (axiom), motsvarande SPARK:s annoterade bibliotek.
   En lista med t.ex. `std::hypot`, `std::fabs` och `std::min`/`std::max` som räknas som
   Stone utan att deras kropp analyseras.
2. **`std::optional<T>`** och liknande som värdetyp. De är inte trivialt kopierbara enligt
   regeln (basklasser), men beter sig som värden.
3. **Muterande parametrar** (`T&`) som en egen kategori, "in out" i SPARK-termer. Det
   ligger närmare Bronze/flödesanalys.
4. Börja på **Silver** för de funktioner som redan är Stone. (Gjort: `--silver`.)
