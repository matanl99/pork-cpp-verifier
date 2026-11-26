# En generell modell för Stone

Status: steg 1–5 byggda (modellen alltid på), `adapters/clang/LoanCheck.cpp`,
regel STN-62. Se [Steg 1: vad som byggdes](#steg-1-vad-som-byggdes) och
[Steg 2: sammanfattningar för egen kod](#steg-2-sammanfattningar-för-egen-kod) och
[Steg 3: std-sammanfattningar som data](#steg-3-std-sammanfattningar-som-data) och
[Steg 4: std.txt mot körning](#steg-4-stdtxt-mot-körning).
Principer för tillit: se [Tillit utan urholkning](#tillit-utan-urholkning).

## Problemet

Stone har vuxit fall för fall. Varje nytt idiom har fått en egen regel i `spec/1_stone.txt`
och egen kod i `adapters/clang/LocalCheck.cpp`: get_if-pekare (STN-39), `const T*`-retur
(STN-11), accessorer (STN-37), `vector<bool>` (STN-38), span (STN-41), range-for (STN-35),
iteratorer från `find` (STN-58), `map::at`/`operator[]` (STN-59), `set::insert` (STN-60).
Den 2026-11-22 har spec 62 regler och `LocalCheck.cpp` 2 400 rader.

Nästan alla de fallen är samma två saker i olika form:

- **ett värde som lånar något** (referens, pekare, iterator, span, proxy), och
- **ett anrop med en effekt** (läser, skriver, ogiltigförklarar lån).

När de sakerna är kodade fall för fall kan en kombination saknas utan att någon märker det.
Ett exempel: `std::move` av en behållare med en levande iterator in i den släpptes igenom
tills en egen kontroll lades till (`moved_container` i `corpus/47_iterator_loans.cpp`).
I en gemensam modell hade det följt av regeln "ett anrop som ändrar `m` avslutar lånen av `m`".

Målet: ett nytt idiom ska kräva en **sammanfattning** (data), inte en ny regel (kod), och
säkerheten ska gå att argumentera för utifrån några få garantier.

## Garantin

En Stone-funktion uppfyller:

- **G1 Känd kod.** All kod som kan köras är analyserad eller täckt av en sammanfattning
  (redovisad som antagande).
- **G2 Inga döda lån.** Ingen åtkomst via ett lån efter att det lånade objektet har dött
  eller lånet har ogiltigförklarats.
- **G3 Inget dolt alias vid anrop.** Två operander till samma anrop (objektet, argumenten
  och det lambdor och referensmedlemmar når) kan inte vara samma objekt om en av dem ändras.
- **G4 Synliga effekter.** En funktion läser och skriver bara det som syns i signaturen:
  parametrar, `*this` och det de når. Inga globaler.

G3 gäller anrop, inte lokala alias inom en funktion: där ser Silver båda namnen och modellerar
aliaset. Det är vid anropet som aliaset blir osynligt, för den anropade funktionen antar att
dess parametrar är skilda.

**Designbeslut:** G3 gäller vid anrop, inte för lokala alias. Två levande
mutabla referenser till samma behållare i en funktion (`second_element`, `borrow_across_loop`
i `corpus/13_local_refs.cpp`, `write_while_borrowed` i `corpus/15_access.cpp`) är tillåtna i
modellen. Skäl: aliaset är minnessäkert (G2 håller), och Silver ser båda namnen och
modellerar aliaset; det är bara vid ett anrop som aliaset blir osynligt för den som antar
skilda parametrar. Reglerna i `LocalCheck` är striktare och avvisar dem; det är en känd,
avsiktlig oenighet och ingen lucka i modellen. När `LocalCheck`s lånespecialfall tas bort
följer Stone modellens linje.

En ändring räknas försiktigt: `std::set::insert` under iteration är definierat i C++ (noderna
flyttas inte), men utan en sammanfattning som säger det ogiltigförklarar den lånen.

Varje regel i spec ska kunna motiveras med en av G1–G4. En regel som inte kan det är antingen
en begränsning i analysen (ska stå som lucka) eller fel.

## Tillit utan urholkning

Beslutat innan portar och betrodda klasser byggs. Gäller Stone och Silver.

### Varför

pork behöver tillit: libstdc++ analyseras aldrig, och verklig kod har gränser (I/O, egen
minneshantering) som aldrig blir Stone. Men varje form av tillit är också ett sätt att
urholka vad nivåerna betyder. Två hål som hittades visar hur det ser ut:

- Att lita på `std::vector::emplace_back` betydde i praktiken att lita på användarens
  konstruktor som den kör, och `std::map`/`std::find` dolde användarens `operator<`/
  `operator==` (STN-67). Tilliten täckte mer än det som stod i antagandet.
- `std::unique_ptr` har grunt const: genom en `const unique_ptr&` gick det att skriva, och
  Stone litade på att C++ stoppar skrivningar genom const (STN-70). En egenskap hos en
  betrodd typ bröt ett antagande som inte stod någonstans.

Ingen av dem syntes i rapporten. Det är urholkning: en dom som ser likadan ut men betyder
mindre.

### Principerna

**P1 En dom ändrar aldrig betydelse, bara villkor.** `stone` betyder G1–G4. `stone: assuming
X` betyder G1–G4 *givet X*. Silver likadant. Tillit får aldrig ändra eller stänga av en regel,
bara lägga till ett namngivet antagande. Ett antagande som påverkar domen men inte syns på
raden är ett fel i pork.

**P2 Tillit är smal och har en exakt innebörd.** Varje sort av tillit har en beskrivning av
vad som antas och vad som fortfarande kontrolleras (tabellen nedan). Det som tilliten inte
uttryckligen täcker kontrolleras, även om det ligger "bakom" det betrodda: egen kod som en
betrodd funktion kör är anrop (STN-67), egenskaper hos betrodda typer som bryter Stones
antaganden kontrolleras vid användningen (STN-70).

**P3 Kontrollerad skyldighet före ren tillit.** Där pork kan kontrollera det man litar på ska
det göras, så att det som verkligen är tillit blir så litet som möjligt. Exempel: std-
sammanfattningarna prövas mot körning (Steg 4); lånemodellens sammanfattningar av egen kod
härleds ur kroppen; gränssnittskontrakt bevisas mot implementationerna (Liskov, SLV-38).

**P4 Sorterna går att skilja åt och förbjuda.** Antaganden delas in i sorter:

| Sort | Vad man litar på | Exempel |
|---|---|---|
| **bibliotek** | kod som pork aldrig ser (std, libc) | `std::hypot`, `type std::vector`, std.txt, `contracts/std.c` |
| **egen** | egen kod som undantas från analysen | `pork::trusted`, betrodda klasser (förslag) |
| **gräns** | kod på andra sidan en avsiktlig gräns | `dispatch I::f`, portar (förslag) |
| **modell** | en begränsning i Silvers modell | `std::vector size <= 16` |

En körning ska kunna säga vilka sorter som är tillåtna, så att "Stone i CI" betyder något
bestämt för ett projekt och en rad inte kan bli grön genom en tyst ändring i trust-filen.
`pork::stone` (krav, exit 1) ska kunna kräva det också.

**P5 Tilliten syns i sin helhet.** Rapporten avslutas med en sammanfattning: antal antaganden
per sort och vilka. Då syns det mellan två körningar om tilliten växer.

### Sorterna som finns idag

| Mekanism | Sort | Vad som antas | Vad som fortfarande kontrolleras | Syns som |
|---|---|---|---|---|
| Trust-fil: funktion (`std::hypot`, `X::*`) | bibliotek | funktionen är Stone, och i Silver dess C-kontrakt | bara funktioner utan definition; egen kod den kör (STN-67); argument, alias, lån vid anropet | `assuming std::hypot` |
| Trust-fil: `type X` | bibliotek | en instans med värdetyper som argument är en värdetyp | typargumenten; grunt const (STN-70); ägande från rå pekare (STN-69) | `assuming type std::vector` |
| std-sammanfattningar (`summaries/std.txt`) | bibliotek | låneeffekterna av std-funktioner | att en rad finns (saknad rad = försiktigt standardval); raderna prövas mot körning | del av antagandet om funktionen |
| C-kontrakt (`contracts/std.c`) | bibliotek | den betrodda funktionens beteende i Silver | att kontraktet finns (SLV-21); förvillkoren vid anropet | del av antagandet om funktionen |
| `pork::trusted` | egen | den märkta funktionen | anropen till den | `trusted` / `assuming f` |
| `dispatch I::f` | gräns | implementationer utanför de analyserade källorna | kända implementationer måste vara Stone; alias och lån vid anropet (STN-54/55) | `assuming dispatch I::f` |
| `--vector-bound` | modell | vektorer är högst N element | allt annat i beviset | `assuming std::vector size <= N` |

Avvikelser från principerna idag: sorterna redovisas inte separat (P4), det finns ingen
sammanfattning (P5), och `pork::stone` kan inte förbjuda en sort.

### Innan en ny sort av tillit byggs

En ny mekanism (portar, betrodda klasser med view/value/owner) ska svara på detta i
spec innan den byggs:

1. **Vilken sort** (P4), och vad heter antagandet på raden?
2. **Exakt vad antas?** En mening, utan "allt som har med X att göra".
3. **Vad kontrolleras fortfarande**, både hos den som anropar och, där det går, hos det
   betrodda (P3)?
4. **Vilket G** (G1–G4) blir villkorat av antagandet, och hur?
5. **Vad kan ett missbruk göra grönt**, och syns det i rapporten?
6. **Korpusfall** för både det som släpps igenom och det som fortfarande avvisas.

Exempel, förslaget om portar (inte beslutat): sort *gräns*; antagande `port I`; antas att
implementationerna bara har effekter utanför programmet; kontrolleras fortfarande att bara
rena gränssnitt kan vara portar, argument/alias/lån vid anropet (STN-54/55), och, om
port-säkerhet byggs, att implementationerna inte sparar lån av argumenten eller når kärnans
typer; G4 blir villkorat (effekterna utanför syns inte i signaturen); ett missbruk kan göra
en kärna grön som anropar en adapter som skriver i kärnans minne, vilket port-säkerheten
skulle stoppa och antagandet på varje rad visar.

## Generella regler i stället för fixar

Skrivet efter en adversariell granskning av ändringarna STN-77 till STN-81 (STN-2, 8, 25, 37/78, 71, 77, 80, 81)
(rapporten är inte publicerad). Den hittade fyra felaktiga godkännanden:

- F1: en oinitierad bas vid en implicit standardkonstruktor.
- F2: `m[0]` och `m[1]` räknades som samma objekt.
- F3: två bas-subobjekt av samma typ räknades som ett.
- F4: ett consteval-resultat vars destruktor körs vid körning.

Granskningen hittade också två inkonsekvenser:

- I1: en skrivning genom ett forwarding-argument inuti ett uttryck.
- I2: `v.begin()` och `std::begin(v)` får olika dom.

Alla kom ur samma sak: en ny konstruktion (bas, consteval, forwarding, nästlad lista) korsades
med en befintlig kontroll som skrevs innan konstruktionen fanns. En specifik fix täcker ett
sådant kryss. Principerna nedan ska täcka hela raden.

Under varje princip står läget efter rättningarna (körningsoraklet, STN-82 och granskning 1–2) och
två granskningar av dem. Det som återstår är samlat sist, under "Kvar".

### P-a. "Kan" och "måste" är olika frågor

En fråga om platser har en riktning:

- **kan vara samma** (alias, invalidering). Förlorad precision ger *ja*.
- **måste vara samma** (intervall, jämförda iteratorer, positioner, STN-77). Förlorad
  precision ger *nej*.

F2 och F3 är samma fel. `mustBeSameObject` byggde på `overlap()`, som är en kan-fråga.

- I F2 matchade indexsteget `[*]` allt.
- I F3 såg vägen igenom DerivedToBase, så `d.L::v` och `d.R::v` blev båda `d.v`.

Regler:

- Frågan svarar `Yes/No/Unknown`, och anroparen bestämmer vad `Unknown` betyder. En kan-fråga
  ska inte gå att använda som en måste-fråga, och det ska typerna se till.
- Varje abstraktion som slår ihop objekt står uppräknad: index → `*`, bas → hela objektet,
  inre lista → `in[*]`. Var och en gör måste-frågan till `Unknown`. En ny sammanslagning
  (unioner, `map[k]`) läggs till i listan, inte som ett specialfall vid anropet.
- Prov: en enda ändring ska stänga F2 och F3 tillsammans, och `std::array<std::array<int,4>,2>`
  ska fungera utan ny kod.

**Läge: delvis.** `mustBeSameObject` är en egen måste-fråga (samma rot, samma bindning, samma
kända väg; STN-77). Hopslagna nivåer räknas (`extra`), och en plats som nåtts genom en
referensbindning bär bindningen (`via`), så `m[0]` och `m[1]` är inte längre samma objekt
och `std::array<std::array<…>>` fungerade utan ny kod. F3 är däremot löst genom att en
upprepad bas med fält avvisas (STN-2), inte genom att sammanslagningen bas → hela objektet ger
`Unknown`. Det är sunt men striktare än principen. Typerna `Yes/No/Unknown` finns inte än:
att en kan-fråga inte går att använda som måste-fråga hålls av namn och granskning, inte av
typerna.

### P-b. G1 härleds ur CFG:n, inte per konstruktion

F4 kom av att STN-80 påstod att "ingen kod körs" utan att någon kontrollerade det. Clangs CFG
(`AddImplicitDtors`, `AddTemporaryDtors`, `AddInitializers`) listar varje konstruktor,
destruktor och temporärförstörelse som faktiskt körs.

Regler:

- Varje anrop och varje destruktor i CFG:n är redovisad: analyserad, eller täckt av ett
  antagande.
- Ett undantag (consteval, triviala funktioner) får bara stryka element som CFG:n inte har med,
  aldrig dölja ett element som den har. Ett undantag tar alltså bara bort en överflödig
  kontroll.
- Samma regel täcker implicit kod som ingen har tänkt på än: NSDMI, defaultade jämförelser,
  temporärer i range-for.

**Läge: klar** (STN-82, `cfgAccounting` sist i `checkFunction`). Varje CFG-element som kör
kod ska vara redovisat som anrop, självt eller via den användarkod det når. CFG:n har
defaultargument och defaultinitierare som löv, så kontrollen följer deras uttryck själv.
Anrop utan känd anropad funktion avvisas. Kontraktsfunktionerna undantas på annoteringen.
Larmar inte i korpus, exempel, rugby eller de fyra externa repona.

### P-c. En genomgång av subobjekt

F1 kom av att genomgången av "fält" fanns i flera kopior. `ctorInits` lärde sig baser, men
`defaultInit` gjorde det inte.

Regler:

- En funktion `subobjects(rd)` returnerar baser och fält i initieringsordning. Den används för
  allt som handlar om initiering, värdetyper, destruktorer och `hasFields`.
- `rd.fields()` utanför den funktionen är en varningsflagga och går att hitta med grep.
- Nästa utvidgning (virtuella baser, anonyma structar) ska bli en ändring på ett ställe.

**Läge: klar.** `subobjects()` används för `hasFields`, användarkoden (`UserCodeScan`),
destruktorer, värde- och objekttyper, konstruktorinitierare och `defaultInit`. De
`rd->fields()` som finns kvar i LocalCheck är genomgångar av vilka typer som går att nå
(portar, dolda referenser, alias). De går igenom baserna separat och rör inte initiering.

### P-d. En sammanfattning uttalar sig bara om sin egen kod

`owner value_or : reads` och regeln "en std-konstruktor läser sina forwarding-argument" (I1)
påstår saker om användarkod som std bara vidarebefordrar till: en konvertering, en konstruktor,
ett predikat.

Regler:

- En rad i std.txt beskriver bibliotekets egen effekt på behållaren och `*this`.
- Vad som händer med ett argument som når användarkod avgörs av den kodens egen analys, som
  för `operator<` i STN-67.
- `reads`/`access` gäller inte ett `U&&` som skickas vidare. `check_summaries.py` kan avvisa
  det mekaniskt.

**Läge: klar i sak, spärren saknas.** Kommentaren för `value_or` i std.txt säger att raden
bara gäller `*this`. Ett forwarding-argument till en std-konstruktor räknas som läst bara
när den användarkod det når inte kan skriva (`userCodeMayWrite`), och undantaget via
`libraryReads` är borttaget. Det finns ingen mekanisk spärr i `check_summaries.py`, så en ny
rad i std.txt kan göra samma misstag igen.

### P-e. Korsningarna är testmatrisen

Korpusen är ordnad efter regel, men hålen ligger i korsningarna mellan regler.

- **Matris.** Varje ny konstruktion korsas med varje kontroll: initiering, alias, samma objekt,
  sidoeffekt i uttryck, destruktor och lån. En tom ruta är ett otestat kryss och ska bli ett
  korpusfall eller en anteckning om varför krysset saknas.
- **Ekvivalenta skrivsätt.** Två skrivsätt av samma sak ska ge samma dom: `v.begin()` och
  `std::begin(v)`, ett deluttryck utbrutet till en lokal, `if` och `?:`. En skillnad i dom
  är antingen ett hål eller en dokumenterad begränsning (I1, I2).
- **Körning som orakel.** De stone-fall i korpusen som har en `main` körs under ASan, MSan
  och `_GLIBCXX_DEBUG` i ctest. Ett larm på något som fick stone är ett fynd, utan att någon
  behöver läsa koden.

**Läge: delvis.** Körningsoraklet finns (`tests/check_runtime.py`, ctest `corpus_runtime`).
Körningarna står i korpusfilerna under `#ifdef PORK_RUN`. Ett självtest med en felaktig rad i
std.txt ger HOLE. Tvetydiga körningsnamn och program som kraschar vid listningen fäller
testet. Ett andra orakel låter kompilatorn utvärdera samma körningar med constexpr
(`tests/check_constexpr.py`): odefinierat beteende i kärnspråket måste avvisas där, också det
som ASan inte ser (råa pekare i olika objekt, delete via en bas utan virtuell destruktor).
Overflow och index utanför räknas inte som hål, det är Silvers sak. Ett tredje orakel korsar
domarna med Clang Static Analyzer (`tests/check_analyzer.py`, ctest `corpus_analyzer`): en
varning vars körväg bara går genom godkända funktioner är ett kandidathål. Det kräver inga
körningar, så det prövar också filer utan `PORK_RUN`. Division med noll och index räknas som
Silvers sak, kända falsklarm märks `// analyzer: <check>: <varför>`. Matrisen och kontrollen av
ekvivalenta skrivsätt finns inte. I2 hittades av en
granskare, inte av en test.

### Prov på en rättning

En rättning ska formuleras per princip, inte per fynd. Om en fix bara stänger exakt det fall
som granskningen skrev, är den specifik och inte generell.

Så gick provet för P-b: F4-rättningen i STN-80 togs tillfälligt bort, och korskontrollen
hittade F4 ändå (`'Leak::~Leak' runs here`). För granskarens prober (defaultargument,
aggregat, pekaranrop, kod i namespace pork, cleanup) kopplades reglerna tillfälligt bort
(tom `callees`), och kontrollen larmade på alla.

### Kvar

- P-a: frågorna som `Yes/No/Unknown` i typerna, och F3 som en uppräknad sammanslagning
  (bas → `Unknown`) i stället för avslag.
- P-d: spärr i `check_summaries.py` mot `reads`/`access` på funktioner med ett `U&&`-argument
  som skickas vidare.
- P-e: matrisen konstruktion × kontroll, och test att ekvivalenta skrivsätt
  (`v.begin()`/`std::begin(v)`, utbrutet deluttryck, `if`/`?:`) ger samma dom.

## Modellen

### Platser

En plats är en väg: rot (lokal variabel, parameter, `*this`) plus fält, som dagens `Path`.
Ett element i en behållare är en **öppen** väg (`v[*]`): pork vet inte vilket element, så
två öppna vägar in i samma behållare kan överlappa.

### Lån och lånebärande värden

Ett lån har en plats och ett slag: **delat** (bara läsning) eller **exklusivt** (läsning och
skrivning). Ett lånebärande värde är ett värde vars typ bär lån: referens, pekare,
iterator, `std::span`, `std::string_view`, `vector<bool>::reference`. clang känner redan till
de flesta std-typerna som `[[gsl::Pointer]]` (lånar) och `[[gsl::Owner]]` (äger)
(`isGslPointerType`/`isGslOwnerType` i `clang/Analysis/Analyses/LifetimeSafety/LifetimeAnnotations.h`).

Ett lånebärande värde får bara hållas i en lokal variabel (inte fält, inte returnerat, utom
enligt signaturen) och får bara användas genom operationer som har en sammanfattning.

### Livslängd

Ett lån lever till sista användningen av värdet som bär det eller av ett alias till det
(clangs LiveVariables över CFG:n, som pork redan använder, STN-34). Ett lån som aldrig
används dör direkt.

### Kontrollen

En plats har en väg av fält och steg `[*]`. Ett steg `[*]` är **ägd lagring**: element i en
behållare, alternativet i en variant, det en iterator pekar på. Ägd lagring kan försvinna
när ägaren ändras; fält ligger inline och överlever.

- **Ändring av X** (tilldelning, icke-const anrop, T&-argument, `std::move`): ett levande
  lån av något i ägd lagring under X är ett fel (G2). Ett lån av X självt, eller av ett
  fält i X, överlever: en referens till ett objekt gäller fortfarande efter att objektet
  fått ett nytt värde.
- **X dör** (scope slut): ett levande lån av X eller något i X är ett fel. En variabel som
  lånar en temporär och används efter satsen är ett fel.
- **Anrop**: två operander som kan nå samma objekt, där en ändras, är ett fel (G3). Två
  iteratorer eller vyer får överlappa (ett iteratorpar). Två iteratorer som jämförs måste
  peka in i samma objekt (annars UB).
- **Retur och lagring**: ett lån av en lokal variabel, värdeparameter eller temporär får
  inte returneras eller lagras i något annat än en lokal variabel.
- **Yttre objekt**: parametrarnas mål och `*this` är skilda (anroparen har kontrollerat
  G3). Målet för en referensmedlem är ett okänt yttre objekt som kan vara samma som varje
  annat yttre objekt av en typ som når det. Ett anrop som når ett polymorft objekt (en port)
  kan ändra allt utanför funktionen.
- **Okänt**: ett uttryck som bär lån och som modellen inte förstår (pekararitmetik) är ett nej.
- **Tillåtelselista** (designbeslut): modellen är en tillåtelselista på egen hand,
  som reglerna, inte bara tillsammans med dem. Varje element i CFG:n är antingen ett slag
  som modellen hanterar (deklaration, tilldelning, `++`/`--`, anrop, konstruktion, `return`,
  initierare, scope-slut) eller ett slag som uttryckligen saknar låneeffekt
  (`withoutLoanEffect`: literaler, läsningar, aritmetik, jämförelser, `static_cast`/C-cast utan
  pekarbyte, `sizeof`, lambdauttryck). Allt annat (asm, `new`/`delete`, `throw`,
  `reinterpret_cast`, `const_cast`, GNU-tillägg) ger `cannot follow the loans of <nod>`. En
  referens utan bindning på en väg och en statisk lokal variabel som lagrar ett lokalt lån är
  också nej. Skäl: "bergsäker" ska gälla modellen ensam; ett okänt sätt att skriva till minne
  får aldrig passera tyst.

Det är hela kontrollen. Resten är sammanfattningar.

## Sammanfattningar

En sammanfattning beskriver en funktion i ett litet, fast språk:

```
function std::map::at          # const-överlagringen och den icke-const
  reads   this, key
  returns borrow this[*]  shared-if-const-this
  invalidates none

function std::vector::push_back
  reads   value
  writes  this
  invalidates this[*]           # alla elementlån (omallokering)

function std::map::insert
  writes  this
  invalidates none              # noder flyttas inte (G2), men skrivningen kräver ändå
                                # att inga lån av this lever (G3)

function std::get_if<T>
  reads   arg0
  returns borrow *arg0 nullable  # samma slag som arg0

function operator==(iterator, iterator)
  reads   arg0, arg1
  requires same-origin arg0 arg1 # jämförelse bara inom samma behållare
```

- **Egen kod**: sammanfattningen räknas fram nedifrån och upp (som dagens fixpunkt i
  `core/Classify.cpp`) och sparas i `FunctionFacts`. Det ersätter dagens specialregler för
  `const T&`- och `const T*`-retur (STN-10, STN-11) med en enda regel: returen lånar det som
  den returnerade vägen lånar.
- **Bibliotek (std)**: sammanfattningar som data i en fil, granskade mot C++-standarden.
  De flesta följer av en kategori (sekvensbehållare, nodbaserad associativ behållare,
  oordnad behållare, vy), så en mall per kategori täcker många funktioner.
- **Försiktig standard**: en betrodd funktion utan sammanfattning skriver genom alla
  icke-const referensargument, returnerar lån av alla referensargument och ogiltigförklarar
  allt den skriver. Sunt men oprecist. Precision kommer från bättre sammanfattningar, inte
  från kod i checkern.

Sammanfattningarna för std går inte att bevisa (libstdc++ analyseras inte), så de är en del av
det vi litar på utan bevis. De är data som går att granska och testa:
`-D_GLIBCXX_DEBUG` upptäcker ogiltiga iteratorer när programmet körs, och ASan dinglande
referenser. Genererade program som Stone godkänner, körda med båda, testar checkern och
sammanfattningarna tillsammans.

## Vad som ligger kvar utanför modellen

Modellen ersätter bara lån och alias. Det här finns kvar som i dag:

- tillåtelselistan för AST-noder (STN-18, STN-19 m.fl.): okända noder är not-stone;
- typreglerna (värdetyper, objekttyper, portar: STN-14, STN-50–55);
- sidoeffekter bara på satsnivå (STN-20, STN-21): en fråga om ordning i uttryck, inte om lån;
- transitivitet och trust (STN-23, STN-24), kontrakt (STN-42–44, STN-56).

Reglerna som bör följa av modellen: STN-10, 11, 26, 27, 31, 33–39, 41, 57–60.
STN-61 (map från klammerlista) och strängliteral via `std::pair` handlar om temporärer och
konstruktorer, inte lån; de hör till tillåtelselistan.

## Prov: sex fall uttryckta i modellen

Fallen är dagens specialfall. För vart och ett: sammanfattningen som behövs och hur dagens
korpusfall avgörs av den generella kontrollen.

| # | Fall (korpus) | Sammanfattning | Korpusfallen i modellen |
|---|---|---|---|
| 1 | get_if (`16_variant.cpp`, STN-39) | `get_if`: returnerar lån av `*arg0`, nullable | `p` lånar `v`; `v = 2.0` skriver `v` medan lånet lever → G3. Test mot `nullptr` och `*p` är läsningar via lånet. Inget eget kodfall behövs. |
| 2 | `const T*`-retur (`19_nullable_return.cpp`, STN-11) | Egen kod, inferens: `first(v)` returnerar lån av `v[*]` | Anroparen får ett lån av argumentets plats. Argument som är temporär → lånet dör vid satsens slut, senare användning → G2. |
| 3 | `map::at`/`[]` (`48_map_element_loans.cpp`, STN-59) | `at`: lån av `this[*]`; `operator[]`: skriver `this`, sedan lån av `this[*]` | `index.clear()` medan `value` lever → G3/G2. `index[key] + 1` avvisas av sidoeffektsregeln (STN-20), som i dag. Nyckelargumentet lånas inte, eftersom signaturen säger att returen bara lånar `this`. |
| 4 | iterator från `find` (`47_iterator_loans.cpp`, STN-58) | `find`: lån av `this[*]` (iterator); `==`/`!=`: kräver samma ursprung; `*`/`->`: läsning via lånet | `it == b.end()` med `it` från `a` → olika ursprung, fel. `m.clear()` eller `std::move(m)` medan `it` lever → G3/G2. Kopia `auto copy = it` är ett alias, inte ett fel (i dag avvisas den). Kravet på en const-vy (`const auto& view = m`) försvinner: ett delat lån av `m` räcker. |
| 5 | elementreferens i vektor (`13_local_refs.cpp`, STN-33) | `operator[]`: lån av `this[*]`; `push_back`: skriver `this`, ogiltigförklarar `this[*]` | Som i dag, men utan egen kod: samma regel som fall 3. |
| 6 | `set::insert` som sats (`49_set_insert.cpp`, STN-60) | `insert`: skriver `this`, returnerar `pair<lån av this[*], bool>` | Resultatet används aldrig → lånet dör direkt; ingen specialregel för "kastat resultat". Insert under iteration → G3. `.second` läser ett `bool` ur paret och blir också godkänt (i dag avvisat, gap 13). |

Slutsats: alla sex går att uttrycka med de fyra kontrollerna och en sammanfattning per
funktion. Tre saker blir bättre än i dag utan extra kod: kopierade iteratorer (fall 4),
`find` på icke-const behållare (fall 4) och `insert(...).second` (fall 6, gap 13 i
`corpus/30_gaps.cpp`). Gap 8 och 9 (`m[k].push_back(x)`, `m[k] = x`) löses om
sidoeffektsregeln förstår att `operator[]` och anropet efter är sekvenserade, vilket är en
fråga för STN-20, inte för lånen.

## Prov: clangs LifetimeSafety

clang 23 har en lånesanalys i Polonius-stil (`clang/Analysis/Analyses/LifetimeSafety/`:
loans, origins, `LoanPropagation`, `LiveOrigins`, `MovedLoans`, `isInvalidationMethod`), som
används av `-Wlifetime-safety`. Prövad på osäkra varianter av de sex fallen
(`-Wlifetime-safety-strict -Xclang -flifetime-safety-inference`) och mot pork:

| Osäkert fall | clang | pork i dag |
|---|---|---|
| get_if-pekare används efter `v = 2.0` | missar | avvisar (G3) |
| `const T*` från egen funktion, argumentet temporärt | hittar (med inferens) | avvisar, men av fel skäl (`&v[0]` i `first` och initializer_list) |
| `map::at`-referens efter `clear()` | hittar | avvisar |
| `find`-iterator efter `clear()` | hittar | avvisar, men av fel skäl (icke-const iterator) |
| elementreferens efter `push_back` | hittar | avvisar |
| `set::insert` under iteration | tyst (inte UB) | avvisar (G3) |
| elementreferens, sedan `grow(v)` (egen funktion som gör `push_back`) | **missar** | avvisar |
| elementreferens, sedan `b.add(2)` (metod som gör `push_back`) | **missar** | avvisar |
| elementreferens, sedan `push_back` via lokalt alias `auto& w = v` | hittar | avvisar |

I de här exemplen gav clangs analys ingen varning vid anrop till `grow(v)` eller `b.add(2)`:
okända anrop antas inte ogiltigförklara något. Stones lånregler behöver också ta hänsyn till
effekterna av sådana anrop, så analysen kan inte ensam avgöra Stone. Men den har
vokabulären vi behöver (loans, origins, gsl Owner/Pointer, `[[clang::lifetimebound]]`,
`[[clang::lifetime_capture_by]]`), och attributen kan vara syntaxen för sammanfattningar av
egen kod och bibliotek. Om faktagenereringen (`FactsGenerator`) går att återanvända med en
försiktig standard för okända anrop är en öppen fråga; den är inte ett stabilt API.

## Parallelldrift

Modellen byggs bredvid `LocalCheck`, inte i stället för den:

- Modellen är en egen kontroll i `adapters/clang/` som lämnar egna fakta. Domen för lån och
  alias = båda godkänner. Då är domen minst lika sund som den sundaste av dem.
- Korpusen visar båda domarna. En oenighet är en signal:
  - **`LocalCheck` godkänner, modellen avvisar**: en sammanfattning saknas, eller
    `LocalCheck` har ett hål. Det senare är den sortens fel som är svårast att hitta fall för fall.
  - **modellen godkänner, `LocalCheck` avvisar**: en lucka som stängdes utan ny regel.
    Det mäter om modellen generaliserar.
- **Specialfallen i `LocalCheck` fryses.** Nya lånefall går bara till modellen. Ett
  specialfall tas bort när modellen täcker det och båda har varit överens på korpusen,
  rugby och övriga mätobjekt.
- Slutkriterium: inga lånespecialfall kvar i `LocalCheck` och inga oenigheter. Fram till dess
  är antalet oenigheter och kvarvarande specialfall måttet på hur långt övergången kommit.

## Öppna frågor

- Fältkänslighet: hur långt ner i fält och element vägar ska följas (i dag fält ja, element
  nej). `v[i]` och `v[j]` är alltid överlappande.
- Sammanfattningar över TU:er: egna sammanfattningar måste in i `FunctionFacts` och kärnan,
  som `contextRefs` i dag, eftersom anropade funktioner kan ligga i andra TU:er.
- Lambdor och fångster: en lambda som fångar med referens är ett lånebärande värde.
- Kontextreferenser och portar (STN-53–55) är en form av lån som lever i ett fält. Passar de i
  modellen, eller förblir de typregler?
- Formatet för std-sammanfattningar: egen fil, eller clangs attribut via en
  header som pork lägger till (`-include`), så att samma attribut fungerar i egen kod.

## Steg 1: vad som byggdes

`adapters/clang/LoanCheck.cpp` (en funktion i taget, clangs CFG med `AddLifetime`, framåtflöde
av vilka lån varje lokal variabel håller, clangs LiveVariables för "används senare").
`LocalCheck` är orörd (fryst). Flaggan `--loans`:

- `report`: modellens dom skrivs under varje funktion: `  loans: ok; local: ok` eller
  `  loans: rejected: <orsak>; local: rejected`.
- `enforce`: Stone kräver båda; modellens orsak blir `not-stone: loans: <orsak>`.

**Lånebärande typer** avgörs utan namnlistor: referenser, pekare, klasser som clang märker
`[[gsl::Pointer]]` (alla std-iteratorer, `span`, `string_view`), lambdor med referensfångst och
klasser som innehåller något av det. `[[gsl::Owner]]` (behållare, `optional`, `variant`,
`string`) äger sina element och bär lån bara om elementtypen gör det.

**Sammanfattningar** är kategorier, inte funktioner (sedan steg 3 som data i
`summaries/std.txt`):

| Kategori | Effekt |
|---|---|
| metod på std-`gsl::Pointer` | `==`/`!=`/`<`... läser; `*`, `->`, `[]` lånar; `++`, `--`, `=` ändrar bara iteratorn |
| metod på std-`gsl::Owner` | `begin`/`end`/`find`/`at`/`front`/`[]`... lånar ägd lagring utan att ändra; `size`/`empty`/`count`... läser; `map::operator[]` ändrar och lånar; övrigt: standard |
| fria std-funktioner | jämförelser läser; `get_if`, `get` lånar; `move`, `forward`, `as_const` är argumentet |
| sökalgoritmer (`find_if`, `lower_bound`, ... även `std::ranges`) | resultatet lånar första intervallet |
| allt annat | standard: icke-const objekt och T&-argument ändras, exklusiva lån kan skrivas genom, resultatet lånar ägd lagring i alla operander |

**Mätning** (`tests/check_loans.py`, ctest `corpus_loans`):

- Korpusen: 761 funktioner, 608 överens, **0 som bara modellen avvisar**, 153 som bara
  reglerna avvisar. De 153 är nästan alla regler som inte handlar om lån (typer, uttryck i
  tillåtelselistan, initiering, sidoeffekter i uttryck, konstruktorer).
- Luckor som stängs utan ny regel: kopierad iterator, `insert(...).second` (gap 13), range-for
  över en returnerad referens (gap 11), iterator från `std::ranges::find_if` (gap 2), ändring av
  map-värden i en range-for, span-variabler.
- rugby (`src/core/*.cpp`): 0 som bara modellen avvisar; `TurnController::plan` (gap 2)
  godkänns av modellen.
- De osäkra varianterna från provet med clang avvisas alla, även ändring via egen funktion
  och metod som clangs `-Wlifetime-safety` missar (`corpus/51_loan_model.cpp`).

Under bygget hittade jämförelsen två hål i modellen och inga i reglerna: G3 jämförde först
bara referensoperander, inte det lambdor och vyer bär (`set_x(v)`, `add_all(w, w.xs)`), och
kontextreferenser/portar saknades. Båda stängdes med regler om platser, inte om idiom.

### Vad modellen inte gör (och reglerna gör)

- **Ordning inom ett uttryck** (STN-20: sidoeffekter bara på satsnivå). Modellen går element för
  element i CFG:n men kontrollerar inte osekvenserade deluttryck.
- **Typer** (STN-14, STN-50–52): att ett lån inte kan lagras i en behållare eller ett fält beror
  på att typreglerna förbjuder `vector<T*>` och pekarfält. Modellen kontrollerar lagring via
  tilldelning, men inte via anrop (`v.push_back(&x)`).
- **Initiering** (STN-16, STN-17) och konstruktorer.
- **Två referensmedlemmar i samma klass** som kan peka på samma objekt (STN-53): en typregel.

Det är meningen: enforce = båda, och var och en täcker det den är bäst på.

## Steg 2: sammanfattningar för egen kod

En egen funktion sammanfattas ur sin kropp av samma kontroll som granskar den
(`LoanContext`, en per översättningsenhet; anropade funktioner sammanfattas vid behov,
oavsett ordning i filen). Sammanfattningen är två mängder:

- **ändrar**: platser relativt `*this` (plats 0) och parametrarna (plats i+1), till exempel
  `0: players` eller `1: [*].wind` (`[*]` först = det en pekare/iterator/lambda pekar på);
  plus absoluta platser (målet för en referensmedlem eller ett pekarfält, något okänt yttre).
  För varje ändrad plats också vad som gäller för *alla* ändringar dit (`Summary::keeps`):
  elementen behåller sina värden (STN-91) eller flyttas inte alls (STN-95). En enda annan
  ändring av platsen (`clear`, `erase`, en annan gren) gör den till en vanlig ändring.
- **returen lånar**: samma slags platser, med exklusiv/delad.

Vid anropet översätts platserna till anroparens operander. Ändringarna ersätter standarden
"ändrar allt icke-const den når"; G3 räknar en operand som skriven bara om sammanfattningen
ändrar något genom den. Alla ändringar i kroppen går genom samma `change()` som kontrollen
använder, så sammanfattningen kan inte missa en effekt som modellen ser.

Ingen sammanfattning (anropet får standarden) när funktionen
- ligger i std eller i en systemheader (har kategorisammanfattningar; `__gnu_cxx::__normal_iterator`
  ska inte analyseras ur kroppen),
- är virtuell, en template, saknar kropp i TU:n, eller är rekursiv (pågår när den anropas),
- avvisas av modellen (efter första felet saknas effekter),
- sparar ett lån (`storeOutside`), anropar en port, eller ändrar/returnerar via en väg genom
  ett pekarfält: det går inte att uttrycka relativt operanderna,
- når en fångad variabel (lambdakropp).

**Mätning**: korpusen 623 överens, 0 bara modellen, 160 bara reglerna. De nya
"bara reglerna" är precision: `reset_team(t)` ändrar bara `t.wind`, så referenser in i
`t.players` är giltiga (sju fall i `13_local_refs.cpp`), och `retarget` tilldelar bara
`target`:s skalärer (`18_ref_return.cpp`); fallen har `// loans: ok`. rugby: 0 bara modellen.
Fall: `corpus/51_loan_model.cpp` (exakt ändring, transitivt, död före ändring, rekursion,
getter som lånar exakt fältet, G3 oavsett fält, pekarfält).

**Hål som hittades** (fanns sedan steg 1): det en pekare eller iterator i ett yttre objekt
pekar på modellerades som lagring som objektet äger. Eftersom parametrar är skilda från
varandra kunde `h.list->push_back(...)` då aldrig ogiltigförklara ett lån i en annan parameter
`v`. Nu får ett pekarfält en egen rot (som en referensmedlem, kan aliasa via måltypen) och annat
lånebärande innehåll (iteratorfält, behållare med pekare) pekar på "något okänt utanför", som
kan aliasa allt yttre (`pointer_member_direct`).

**Det ett yttre objekt håller** (STN-93, korpus 131): när det yttre objektet är `*this`
eller en parameter är roten "det *this/parametern håller" (`Root::held`), inte "något okänt
utanför". Inne i funktionen aliasar den som "okänt utanför". I sammanfattningen sparas ett
returnerat eller lagrat lån av den som `Ret::held` med objektets plats, och hos anroparen
blir det lånen som operanden håller (`mapRet`, `heldByOperand`). Ett lån av något okänt
utanför vore inte nog: `const char& r = c.cur();` med `c` en lokal `Cursor` över en lokal
sträng `s` ska dö både vid `s += ...` och när `s` dör. Skrivningar och tomhet sparas som
"okänt utanför" (`absolute`).

**Behållare med lånebärande element** (STN-93, korpus 137): en lokal behållare håller en
lånemängd för alla sina element (`localHolder` godtar ägd lagring under variabeln). Insättning
(`storedByCall`, för effekterna `standard` och `insert`) lägger till vad argumenten kan ge elementet:
det ett argument av elementtypen håller (`copiesElement`, `value_type`), och annars det
argumentet betecknar, utom tal, enums och tomma std-objekt som byggs in i element av bara
inbyggda och std-typer (`scalarCopiedInto`, `stdOnly`; inte `std::reference_wrapper`). Ett
anrop på en ägare (`ownerOperand`) ger ett resultat som lånar ägd lagring, inte det
elementen håller. Insättning i `std::deque`, `std::list` och `std::map` (effekten `insert`)
följer lånen som för `std::vector`, och en konverterande tilldelning till en lånebärande lokal
(`o = s`) lägger till det högerledet betecknar i stället för att ersätta mängden (korpus 137).
En std-behållares egen metod skriver inte genom lånen som elementen eller argumenten håller
när elementen inte kör användarkod (`elementsRunNoUserCode`, korpus 138), så samma ursprung
får sättas in flera gånger (`vs.push_back(s); vs.push_back(s);`). Kvar (precision):
`m.emplace(1, v)` två gånger med en `std::vector<int>& v` och `std::map<int, std::span<int>>`
avvisas, eftersom forwarding-argumentet räknas som skrivet.

Inom en TU. Över TU:er (sammanfattningen i `FunctionFacts`, fixpunkt i kärnan) är nästa
steg om anrop mellan filer visar sig behöva det; till dess får de standarden.

## Steg 3: std-sammanfattningar som data

Std-sammanfattningarna ligger i `summaries/std.txt`, inte i koden. Filen byggs in i binären
(CMake genererar `StdSummaries.cpp`); `--summaries <fil>` lägger till eller ersätter regler.
Parsern och uppslagningen är ren kärna (`core/LoanSummaries.{hpp,cpp}`, GoogleTest);
adaptern översätter ett clang-anrop till en fråga: kategori efter gsl-märkningen
(`owner`/`pointer`/`free`), kvalificerat mallnamn, namn, vilka parametrar som är iteratorer,
const-metod. Mest specifik regel vinner; ingen regel ger standarden.

En rad: `<kategori> <namn>... : <effekt> [modifierare...]`, med paragrafen i standarden i en
kommentar. Format: `core/LoanSummaries.hpp`. Nytt utöver steg 1:s kategorier:

| | Betydelse | Används för |
|---|---|---|
| effekt `insert` | skriver objektet (G3), men befintliga element och iteratorer förblir giltiga | `map`/`set`/`multi*`: `insert`, `emplace`, `try_emplace`; `list`: `push_*`, `emplace_*`, `insert` |
| `position` | första argumentet får peka in i objektet (G3 undantar paret) | `insert(p, ...)`, `emplace(p, ...)`, `emplace_hint`, `erase(p)` |
| `range` | alla iteratorargument får peka in i objektet | `erase(first, last)` |
| `consumes` | iteratorn i arg0 dör; tillåtet bara när resultatet tilldelas samma variabel direkt | `it = c.erase(it)` |
| form `(iterator)`, `(iterator,iterator)`, `(iterator,...)` | väljer överlagring efter parametrarna | `erase(it)` mot `erase(key)` |

`consumes` är sunt för att variabeln skrivs över i samma uttryck (förälder i AST:n:
tilldelning till samma lokala variabel). Varje annan form, till exempel `m.erase(it);` följt
av `it->second` eller `auto next = m.erase(it)`, får den vanliga kontrollen. Andra lån in i
behållaren dör fortfarande (försiktigt: för noder dör egentligen bara det raderade elementet,
men modellen skiljer inte element åt). `position` gäller bara första argumentet: i
`v.insert(p, i, j)` får `i` och `j` inte peka in i `v` ([sequence.reqmts]), och G3 avvisar det.

Utan regel (standarden): `vector`/`string`-insättning (omallokering) och insättning mitt i en
`deque`. Insättning i ändarna av en `deque` och `unordered_*`-insättning (rehash) har
`insert invalidatesIterators` (STN-95): elementen flyttas inte, så ett lån som en lokal
referens eller pekare håller lever, medan lån som iteratorer och andra lånebärande värden
håller dör (`holdsAddress`, `change(..., keepsAddresses)` i LoanCheck). Skrivningen spelas
in i sammanfattningen med samma slag, så en egen funktion som bara gör `d.push_back(x)`
behåller referenserna hos anroparen men inte iteratorerna.

**Mätning**: korpusen 627 överens, 0 bara modellen, 167 bara reglerna. Nytt "bara
reglerna": insättning i en `set` medan den itereras (`49_set_insert.cpp`, `51_loan_model.cpp`),
minnessäkert enligt standarden; att loopen kan se nya element är terminering (Silver). rugby:
0 bara modellen. Fall: `corpus/51_loan_model.cpp` (map/list behåller, vector/unordered
ogiltigförklarar, `erase`-loopar för map och vector, `erase` utan tilldelning, annan levande
iterator, tilldelning till annan variabel, insättningsposition, eget intervall i
`insert(p, i, j)`). ctest `summaries_bad_file` och `summaries_override` prövar `--summaries`.

## Steg 4: std.txt mot körning

`tests/check_summaries.py` (ctest `summaries_runtime`, etikett `runtime`, några minuter)
genererar program "låna ett element, gör en operation, använd lånet" för tio behållare
(`vector`, `deque`, `list`, `string`, `map`, `multimap`, `set`, `multiset`, `unordered_map`,
`unordered_set`) x 26 operationer x lån via iterator/referens, plus `erase`-looparna. Varje
program granskas av pork (`--loans=report`, utan debugläge, som användare kör) och byggs med
`-D_GLIBCXX_DEBUG` (ogiltiga iteratorer), `-D_GLIBCXX_SANITIZE_VECTOR` (läsning bortom
`size()`) och ASan/UBSan (dinglande referenser), och körs. Växande operationer körs 64 varv så
att omallokering och rehash sker.

| pork | körning | betyder |
|---|---|---|
| ok | larmar | **osunt**: en rad i std.txt eller checkern är fel; testet fallerar |
| avvisar | larmar | bekräftat |
| avvisar | ren | försiktigt (precision, inte fel) |
| ok | ren | överens |

Självtest (`summaries_runtime_selftest`): med den medvetet felaktiga regeln
`std::vector::push_back : insert` (`tests/data/override.summaries`) måste skriptet hitta osunda
fall; det gör det (debugläget och ASan, en vardera).

**Resultat**: 322 program som kompilerar: 120 överens, 97 bekräftade avvisningar, 105
försiktiga, **0 osunda**. Under arbetet hittades:

- **Riggen själv**: ASan avslutar med kod 1, som först lästes som ett returvärde: larm räknades
  som ren körning. `main` returnerar nu alltid 0, allt annat är larm, och självtestet visar att
  larm upptäcks.
- **Precision**: `std::map::operator[]` sätter in utan att flytta noder ([map.access]) och är
  nu `insert` i stället för `writeAccess` (körningen bekräftar). Följdfel: resultatet av
  `insert` lånade alla operander (nyckeln också) och gav falsk G3 i `index[key].push_back(key)`;
  resultatet av en insättning lånar nu bara objektet.
- **ASan-bygget**: ett falskt `use-after-poison` i pork på `list.pop_front` (även med
  `--loans=off`): LLVM-headers inbyggda i pork är instrumenterade men `libclang-cpp.so` inte.
  Testpresetens miljö har `ASAN_OPTIONS=allow_user_poisoning=0` (se `CMakePresets.json`).

**De försiktiga** (93, `--list`) är kända luckor, inte fel i std.txt: radering/`pop_*` av ett
*annat* element (element-identitet saknas), `deque::insert(c.end(), x)` (positionen är inte
känd), `rehash`/`reserve` med iteratorlån när körningen inte råkar rehasha, `string` och
`vector::clear` där inget minne frigörs. **Blinda fläckar** i riggen: `std::string` har inga
säkra iteratorer i debugläget, och radering på plats i en sträng frigör inget, så en felaktig
regel för `string` skulle bara hittas när omallokering sker. En ren körning är inget bevis.

## Steg 5: enforce som standard

Modellen körs alltid; Stone kräver att både reglerna och modellen godkänner. `--loans=enforce`
är standard, `--loans=report` ger samma (sunda) dom och skriver dessutom domarna var för sig
(`FunctionFacts::rulesRejected`), `off` finns inte längre. Det är förutsättningen för att ta
bort låneregler ur `LocalCheck`: utan modellen skulle Stone då vara osunt.

## Steg 6: idiomreglerna borta ur LocalCheck

Med enforce som standard kunde lånereglerna i `LocalCheck` ersättas av en generell typregel:
**lånevärde** (STN-39, `TypeRules::isLoanValue`), en typ som vore en värdetyp om den inte bar
lån: pekare till objekttyper, std-typer märkta [[gsl::Pointer]] och std-mallar som inte är
[[gsl::Owner]] (betrodda eller med trivial destruktor) med minst ett lånevärde bland
argumenten. En behållare av lånevärden (`vector<int*>`) är inget lånevärde. LocalCheck släpper
igenom lånevärden som lokaler, uttryck och kasserade resultat; lånemodellen följer lånen.

| Regel | Före | Nu |
|---|---|---|
| STN-60 | `set::insert` som sats | varje kasserat std-resultat som är ett lånevärde |
| STN-59 | referens till `map::at`/`operator[]` lånar mappen | effekten i std.txt (access/writeAccess/insert) avgör |
| STN-58 | iterator från const `map/set::find`, jämförd med `end()` | vilken iterator som helst; modellen kontrollerar |
| STN-39 | `get_if`-pekare: testa, jämför, dereferera; ingen omflyttning | pekare som lånevärde; `&x`, omflyttning; ingen aritmetik |
| STN-11 | returnerad pekare: listade former | vilket uttryck som helst; modellen: inget lån av lokal/temporär |
| STN-41 | span som parameter, källan som `const&` i aliasingregeln | parametertypen kvar; lokala spans, aliasing via modellen |
| STN-61 | `{...}` till `std::map`:s konstruktor | till vilken betrodd std-funktion som helst |

Kvar som regler: STN-38 (vector<bool>-proxyn är elementet, inget lån) och STN-57
(`std::move`: Silver översätter en flytt som kopia). Silver översätter bara de gamla formerna;
allt annat ger `not silver: cannot translate to C` (CTranslate avvisar t.ex. tilldelning av
pekare och `*p` för andra pekare än get_if-pekare). Korpusen: `corpus/52_loan_values.cpp`.
"Only loans rejects" i `check_loans.py` är nu de lån som bara modellen kontrollerar.

## Nästa steg

**Gjort:** mätningen nedan (tinyxml2, fmt, leveldb, yaml-cpp, state; tabell i README
"Mätning mot vanlig C++"). Den gav STN-63–70 (rekursiva typer, `static_assert`, mallparametrar,
konstruktorer, egen kod i std, destruktorer, `unique_ptr`, grunt const). Kvar: pekar- och
referensfält, polymorfa fält och returtyper som inte är värden. Nästa beslut: portar och
betrodda klasser, prövade mot [Tillit utan urholkning](#tillit-utan-urholkning).

**Ursprunglig plan: mät Stone på vanlig C++-kod.** Hittills är
Stone bara mätt på kod som skrevs med pork i tankarna (rugby-kärnan: 58 av 76) eller väldigt
enkel kod (Juliet: 148 av 368). pork:s egen `Classify.cpp` gav 1 av 7, vilket säger mest om
verklig kod. Utan mätning prioriterar vi luckor efter gissning.

- Repon, utan att skriva om dem: `knut-core`, `logger` (båda), `tdd`, `state`, `cnt` är Go
  och utgår. Varje repo behöver `compile_commands.json` och en trust-fil för std-typerna.
- Räkna per repo: andelen Stone, och de vanligaste orsakerna till not-stone grupperade efter
  första orsak (t.ex. `constructor`, `parameter ... pointer`, `calls ... with no definition`,
  `loans: ...`). Räkna också hur många funktioner som faller bara för att de anropar en
  funktion som inte är Stone (transitivt).
- Resultatet styr ordningen mellan: egna konstruktorer (medlemsvis initiering av värdetyper),
  sammanfattningar över TU:er (punkt 1 nedan), `std::string`/`std::function`, och
  ekvivalensen för Silver (`docs/equivalence.md`).
- Spara mätningen som en tabell i README (som rugby-tabellen) med datum.

(Silver: ekvivalens för översättningen, se `docs/equivalence.md`, kommer efter mätningen.)

1. (Klart inom en TU, se Steg 2.) Över TU:er: sammanfattningen i `FunctionFacts`.
2. (Klart, se Steg 3.) Iteratorlån skiljs från referenslån vid insättning (`unordered_*`,
   `deque::push_back`, STN-95). Kvar: `erase` som bara ogiltigförklarar det raderade
   elementet i nodbehållare (kräver element-identitet).
3. (Klart, se Steg 4.) Utöka `check_summaries.py` med fria funktioner (`get_if`, sökalgoritmer,
   `std::ranges`) och iteratoroperationer när std.txt får nya rader för dem.
4. (Klart, se Steg 6.) Ta bort specialfall i `LocalCheck`.
5. (Beslutat: G3 bara vid anrop, se Garantin.)
