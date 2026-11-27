# Bronze: flöde (`--flow`, beroendeoraklet och `PORK_DEPENDS` byggda)

Beslut efter ett idéunderlag (ej publicerat).

## Nivåerna är kumulativa: Stone ⊆ Bronze ⊆ Silver

Precis som i SPARK är en Silver-funktion också Bronze, och en Bronze-funktion
också Stone. En funktion som bryter mot ett deklarerat flödeskrav får aldrig
domen silver, även om den bevisas fri från körtidsfel.

Varför SPARK lägger flödesanalysen under bevisen: bevisen bygger på tre saker
som flödesanalysen garanterar.

1. **Initiering.** Bevisverktyget modellerar inte oinitierade värden.
2. **Inga alias.** SPARKs aliasregler kontrolleras av flödesanalysen; utan dem
   är bevisen osunda.
3. **Ramvillkor (`Global`).** Efter ett anrop måste beviset veta vad som kan
   ha ändrats.

Det fjärde, `Depends` (vad som beror på vad), används inte av bevisen. Att det
ligger under Silver är ett val: nivåerna är en trappa, inte bara ett tekniskt
beroende. Pork gör samma val, så att "silver" också betyder att flödet håller.

## Porks Stone gör redan SPARKs Bronze utan kontrakt

| SPARK Bronze | pork |
|---|---|
| Initiering före läsning | Stone (läsning före tilldelning avvisas) |
| Inga alias | Stone, G3 (lånemodellen, STN-62) |
| Ramvillkor (`Global`) | Stone: inga globaler, och signaturen är ramvillkoret (bara icke-const referensparametrar kan ändras; G4 ser till att signaturen inte ljuger) |
| `out`-parametrar | Strängare: en oinitierad lokal får inte bindas till `T&`, inte ens till en funktion som alltid skriver den |
| `Depends` | Saknas (Bronze nedan) |

Porks nivåer motsvarar alltså inte SPARKs exakt: porks Stone är ungefär SPARKs
Stone plus det mesta av deras Bronze utan kontrakt.

## Vad domen bronze ska betyda

- **Utan deklarerade flödeskrav:** samma som Stone. Ingen funktion förlorar
  Silver för att den saknar flödeskrav.
- **Med flödeskrav** (syntax ej bestämd, t.ex. `PORK_DEPENDS(audit, price)`
  bredvid `PORK_PRE`): dessutom ska varje krav hålla mot det härledda flödet.
  Ett brutet krav fäller Bronze och därmed Silver.
- **Det härledda flödet** är information, ingen dom: ett eget besked, som
  `assuming` i dag.

## Det härledda flödet

Per funktion, för normal retur (som SPARK: en loop som aldrig slutar och ett
undantag räknas inte):

- för varje utdata (returvärdet, varje icke-const referensparameter och dess
  fält, `*this` för icke-const metoder): vilka indata det kan bero på,
  inklusive styrberoenden (villkor som avgör vilket värde som når utdata) och
  sitt eget ingångsvärde när en skrivning kan utebli;
- vilka platser som skrivs **möjligen** och vilka som skrivs **garanterat**
  (på varje normal returväg).

Byggs på lånemodellens sammanfattningar (platser, vägar, vad en funktion
ändrar) och återanvänds vid anrop och över översättningsenheter. Betrodd kod
utan flödesdata i std.txt är konservativ: varje utdata beror på alla indata
och på sitt ingångsvärde, och ingen skrivning är garanterad. Bara data i
std.txt får göra det snävare, aldrig en gissning.

### Satser utan verkan (BRZ-7)

`PORK_DEPENDS` är en övre gräns: en utdata får bara bero på det som står där. En funktion
som beror på för lite uppfyller den alltid, till exempel `if (valid);` (den tomma satsen
är hela if-kroppen) eller `static_cast<void>(round_is_open(answer));` (kontrollen kastas).
Därför listar flödet också satser och villkor vars värde inte når någon utdata, som SPARK:s
"ineffective statement":

```
count_vote: stone
  flow: return <- count
  flow: no effect: the condition at line 5
```

Också utan krav och utan `--flow` skrivs varje fynd som en varning på stderr, en gång per plats
(mallens instanser delar den), utan att domen eller exitstatus ändras (`--no-warnings` stänger av):

```
empty_guard_case.cpp:5:9: warning: the condition has no effect
```

Korpusen förväntar varje varning med `// expect-warning: <meddelande>` på raden ovanför.

En funktion med krav är då inte bronze. Att nå en utdata betyder att värdet, direkt eller
genom senare satser, villkor och anrop, når returvärdet, en utdataparameter vid retur eller
där funktionen kastar, eller ett anrop av okänd eller betrodd kod (eller av en funktion med
sådan verkan, eller som kan kasta). Det räknas med en markör per sats och villkor i samma
analys som beroendena (`findNoEffect` i `core/Flow.cpp`), efter fixpunkten. En const-metod i en klass med referens- eller
pekarfält ändrar `*this` som en icke-const metod (`game_.x = 1` skriver genom referensen), också
vid anropet. Rapporteras:
initiering av en lokal (inte en range-for-variabel), tilldelning, ett anrop som är hela
satsen och villkoret i if, while, for, do och switch; grenarna i `a && b` och `c ? x : y` är
delar av ett värde. Kod som aldrig nås rapporteras inte.

## Ordning

1. **`--flow`: utskrift av det härledda flödet, utan dom.** Inga nivåer ändras.
   **Byggt.** Adaptern (`adapters/clang/FlowBuild.cpp`) gör en
   flödesgraf av kroppen; kärnan (`core/Flow.cpp`) räknar beroenden,
   styrberoenden (postdominatorer) och säkra skrivningar, och sätter ihop
   sammanfattningarna över anrop och TU:er med fixpunkt. Bara för Stone-
   funktioner. "not computed: …" när en skrivning inte syns som en plats:
   iteratorer/pekare/vyer som skrivs genom (`std::sort(v.begin(),
   v.end())`), destruktorer med egen kod (en implicit eller `= default`-
   destruktor följs ner i baser och fält, och en mall i standardbiblioteket
   ner i sina typargument, så en struct med `std::vector<int>` är inte egen
   kod men `std::optional<Guard>` med egen `~Guard` är det;
   `tests/data/flow/11_destructors.cpp`), bas-initierare, en lambda som
   används som värde. En lambda direkt som argument (`std::visit`,
   `std::ranges::sort`) eller en lokal lambda (`auto f = [..]{..}; f(x)`)
   når omvärlden bara genom sina fångster: per värde läses (för en lokal
   lambda värdet när den skapades), per referens kan skrivas, och anropet
   är okänd kod (`tests/data/flow/10_lambda.cpp`). Oraklet prövar
   inte lambdor (Silvers C har dem inte). Svartlådetest:
   `tests/data/flow/` (de 16 exemplen + sundhetsfall), `tests/check_flow.py`.
   Rugby: 49 av 58 Stone-funktioner får flöde. En väg som slutar i `throw`
   (STN-83) leder inte till utgången; villkoret före kastet räknas som ett
   styrberoende för det som följer (en överskattning).
2. **Korpus:** de 16 exemplen i `pork_stone_bronze_silver/examples` med
   förväntat flöde, plus kryss som saknas där: skrivning genom lån (`v[i] = x`,
   `*it = x` är svaga uppdateringar), returnerade referenser, lambdor med
   referensfångst, hela structar mot fält, std-algoritmer.
   **Testmatris:** `tests/data/flow/09_matrix/` läser och skriver
   samma element på varje sätt det kan nås: direkt, lokal referens (också med
   indexet ändrat efteråt), hjälpfunktion med `const T&` och `T&`, två nivåer,
   annan TU, fält, const och icke-const metodmottagare, nästlade fält, range-for,
   ett anrop som skriver indexvariabeln, satser med temporärer, styrberoenden
   och garanterade skrivningar. Alla med `// flow oracle: ok`.
3. **Orakel för beroenden (`--flow-oracle`, byggt):** för en
   Silver-funktion och varje del av en utdata (fält, element, storlek,
   resultatet) körs funktionen två gånger i samma C-program: delarna av
   indata som flödet säger att utdatadelen beror på antas lika, resten är
   godtyckligt, och bevisaren visar att utdatadelen blir lika. Ett motexempel
   är `HOLE`. Det prövar alla värden inom gränserna, inte ett urval, och
   återanvänder Silvers testram (indata av alla typer som översätts).
   - **Garanterade skrivningar** prövas av samma sak: "always written" betyder
     att utdatan inte beror på sitt eget ingångsvärde, så ett omskrivet samma
     värde räknas rätt.
   - **Normal retur:** förvillkoren antas, Silver har visat att inga körtidsfel
     och inga kast nås, och ett kast avslutar vägen.
   - **Inte prövat:** funktioner som inte är Silver, och funktioner som når ett
     kontrakt (`std::fabs`, ett PORK_POST-kontrakt): ett kontrakt ger
     godtyckliga värden, så två körningar kan skilja sig utan att indata gör
     det. Också kontextreferenser och konstruktorer.
   - **Självtest** (`--flow-oracle-selftest`): pork tappar ett beroende per
     utdata (med dess delar) före provet; då ska hålen synas.
   - Delar kopplas till flödets platser (`v[*]` och `v` hör ihop åt båda hållen:
     en fast elementmängd låser också storleken). `PORK_PRE` är ett antagande,
     inget beroende (som i SPARK).
   - Utfall: korpusen `tests/data/flow` ok 69, oprövade 31, inga hål;
     självtestet ger 69 hål (ctest `flow_oracle`, `_esbmc`, `_selftest`).
     Mot koden före granskning 2–4 (rättningen av tre tappade beroenden i `--flow`) hittar det alla nio hålen i
     matrisen utan förväntade rader. I rugby hittade det två hål som ingen
     granskning sett (satser med temporärer föll bort); efter
     rättningen ok 30, inga hål.
4. **Krav och domen bronze (byggt, spec/3_bronze.txt).** Valda efter
   riktiga krav: rugbys poängmodul (`in_opponents_in_goal`, `check_try`) och
   fristående exempel (faktura, kvot, källval, jobbstatus,
   tio nivåers anropskedja). Alla behövde samma form, en tillåtelselista per
   utdata: `PORK_DEPENDS(output, allowed...)` och
   `PORK_DEPENDS_RESULT(allowed...)`, först i kroppen. Garanterade skrivningar
   behövdes inte: när en skrivning kan utebli blir utdatans ingångsvärde ett
   beroende, och det står inte i listan (jobbstatus). Varje avsiktlig ändring
   fälls av sitt krav och av inget annat (korpus 91, 92).
   - Ett beroende i flödet utanför listan, en överskattning eller `not
     computed` fäller kravet: pork kan inte skilja en överskattning från ett
     verkligt beroende.
   - Bronze ligger mellan Stone och Silver: ett brutet krav ger `stone: not
     bronze: ...` även om beviset går igenom.
   - Med `--flow-oracle` prövas flödet före domen; en funktion vars krav håller
     men vars flöde oraklet inte kunde pröva får raden "the flow requirements
     rest on a flow the oracle did not check".
   - Flödesgrafen byggs alltid (billigt); flödet räknas bara ut när någon
     funktion har krav, eller med `--flow`.
   - Ett krav som inte håller ger `error: 'f' does not meet PORK_DEPENDS: ...`
     på stderr och utgångsstatus 1, som `pork::stone` (BRZ-6). Det gäller
     också ett krav i en funktion som inte är Stone eller vars kropp är
     betrodd, och ett krav var som helst utanför kontraktsinledningen
     (konstruktorinitierare, lambda, `sizeof`). Inledningen är en och samma
     regel för Stone, översättaren och flödet (`leadingRequirements`):
     PORK_PRE, PORK_POST, inledande assert (också avstängd med NDEBUG,
     `disabledAssert`) och PORK_DEPENDS i valfri ordning. Ett krav i en lokal
     klass hör till metoden, inte till den omslutande funktionen.
   - Ett krav utanför varje funktionskropp hör inte till någon funktion,
     inte heller till en som använder initieraren eller defaultargumentet.
     Ingen uppräkning av platser: varje PORK_DEPENDS i TU:n som ingen
     funktionskropp äger (fältinitierare, initierare för en global, statisk
     medlem eller enumkonstant, defaultargument, static_assert, ...) ger `error: PORK_DEPENDS in the initializer of field 'Cfg::b' is
     outside a function body` på kravets plats, utgångsstatus 1
     (`IFactsSource::strayRequirements`, `IReport::strayRequirement`).
   - Placeringen prövas i källan, också i en mall som aldrig instansieras
     (där är kontraktsanropen olösta uppslagningar, `isRequirementCall`,
     och en assert känns igen som makroexpansion). En oanvänd mall med ett
     felplacerat krav ger `error: PORK_DEPENDS in the body of 'f' must come
     before all other statements`. Felet släpps bara när en instans fel för
     samma förekomst skrivs ut (i Analyse, alltså inom `--only`); en
     förekomst är mallens usr, den verkliga filen och byteoffseten
     (`StrayRequirement::occurrence`, `FunctionFacts::misplacedAt`), inte
     visningsläget efter `#line`. En instans där en `if constexpr`-gren
     väljs bort tar inte bort felet.
     Flödet prövas bara på instanserna.
   - Bronze avgörs före Silvers fixpunkt: en funktion vars krav inte håller
     bevisas fortfarande (oraklet prövar den), men sänks innan anropare
     prövas mot dess eftervillkor, så de faller också.
   - `PORK_DEPENDS(out)` utan tillåtna platser: utdatan är en konstant.
   - Inte än: krav på element med ett visst index.

## Möjlig uppmjukning (inte beslutad)

Med garanterade skrivningar skulle `int a; set_value(a, p); return a;` kunna
godtas, som SPARKs `out`-mod. Det hör i så fall till Bronze, med
sammanfattningen som grund, inte till Stone (nivåerna får inte eroderas).
