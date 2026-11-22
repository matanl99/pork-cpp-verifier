# Ekvivalens: stämmer C-översättningen med C++-originalet?

Silver bevisar egenskaper hos pork:s C-översättning (`adapters/clang/CTranslate.cpp`), inte hos
C++-koden. Ett fel i översättningen kan ge ett falskt "silver", och ingenting har kontrollerat
översättningen. Det är den största kvarvarande risken i verktyget.

## Prototyp: `tests/check_equivalence.py`

ESBMC har en egen C++-frontend (clang) och behöver inte översättningen. Prototypen använder den
för att bevisa likhet per funktion:

- `pork --silver --keep-c` skriver C-programmen (vektorgräns 3).
- Testramen är C++: C-koden inkluderas i `extern "C"`, där `_Bool` blir `bool` och pork:s egna
  kontroller (`__CPROVER_assert`) blir antaganden, eftersom Silver bevisar dem separat. Originalfilen
  inkluderas som den är.
- pork:s `pork_harness` återanvänds för indata och antaganden. Varje argument fylls över till
  parameterns C++-typ (`fill`: vector, string, optional, variant, egna structar via fältnamn; metoder
  via `params<R (C::*)(A...)>`). Båda anropas, och returvärdet och `T&`-argumenten jämförs (`eq`).
- Utfall: `equal`, `DIFFERENT` (motexempel), `trusted` (betrodda funktioner eller anrop via
  `PORK_POST`-kontrakt: C-versionen är en abstraktion och jämförs inte), `esbmc` (ESBMC larmar i
  originalet eller i sin std-modell), `bounded`, `skipped`.
- Kontrollprov: ett planterat fel i en loopgräns (`count_done`) gav motexempel (C 1, C++ 2).

```
python3 -I tests/check_equivalence.py build/pork corpus/20_silver.cpp ... --timeout 180 --keep <dir>
```

### Resultat på 12 Silver-korpusfiler

| Utfall | Antal |
|---|---|
| equal | 155 |
| trusted / contract | 9 + 3 |
| esbmc | 10 |
| skipped | 15 |
| bounded | 2 |
| DIFFERENT, obekräftade | 13 |

`skipped` omfattar: ESBMC saknar `std::span` (29), överlagrade namn (38 `rename`), tidsgränser
(24 `set_any`/`set_speed`) och returnerade structar utan `eq_struct` (19).

Två DIFFERENT har undersökts:

- `grown` (23): observerad avvikelse i ESBMC 8.6. En konkret körning med g++ och libstdc++
  ger 8, som C-versionen. ESBMC ger 7, eftersom den inte nollställer `int hits;` vid
  värdeinitiering (`resize`).
- `field_of` (22): öppet fall, i kombinationen av testram och bevisare. Samma testram ger
  `equal` när källfilen bara har `Vec2`, `small` och `field_of`. Står
  `optional<int>::value_or` tidigare i filen ser det ut som att ESBMC läser flyttalen som
  heltal genom optional-lagringen. Utan pork:s header har det inte gått att återskapa
  fristående, så orsaken är inte fastställd.

Beteenden som observerats med ESBMC 8.6 i den här testramen, och hur testramen undviker dem:

- Värdet gick förlorat när en `std::optional<struct>` returnerades som värde. Därför fylls argumenten
  på plats.
- En skrivning genom en referens in i optional-lagringen lästes med fel typ (float som int). Därför fylls
  en lokal variabel som sedan tilldelas.
- Variant-modellen hade inte `emplace<I>` och `variant_alternative_t`. Därför används tilldelning
  och `decltype(std::get<I>(...))`.
- `std::span` stöddes inte.

Slutsats: i prototypen används ESBMC för att hitta kandidater. "equal" är värdefullt, men ett
"DIFFERENT" räknas inte som ett översättningsfel förrän en konkret körning bekräftar det, eftersom
flera av dem hittills har berott på std-modellerna eller testramen. Ingen översättningsbugg är
bekräftad än.

## Nästa steg här

Först kommer mätningen av Stone på vanlig C++-kod (`docs/stone_model.md`, Nästa steg); sedan:

1. **Konkret omkörning av varje DIFFERENT.**
   - Läs indata ur ESBMC:s motexempel (`pork_a0 = ...`, även nästlade fält och vektorelement).
   - Kompilera C-översättningen med gcc och originalet med g++ mot riktiga libstdc++, och kör båda
     med de indata.
   - Lika resultat betyder att motexemplet inte bekräftades i den körningen (`unconfirmed`);
     olika betyder ett bekräftat översättningsfel.
   - Börja med de 13 obekräftade: 22 `field_of`/`field_of_any`, 23 `grown`, 24 `stop_all`,
     25 `retarget`/`to_run`, 26 `sum_clamped`/`use_keep_small`/`use_lies`, 27 `grown_width`,
     38 `capitalise`/`join`/`join_checked`. Flera returnerar std-typer som värde, där flest
     avvikelser har observerats.
2. **Samma omkörning med slumpade indata** (körningsbaserad differentiell testning) för det ESBMC
   inte kan parsa: span, och `skipped` i allmänhet. Det är den enda kontrollen mot den riktiga
   libstdc++.
3. Täck luckorna i testramen: `eq_struct` för returnerade structar (19), överlagrade namn (välj
   överlagringen via parametertyperna) och funktioner i namnrymder.
4. Därefter ctest med etiketten `runtime` (som `summaries_runtime`), med de kända obekräftade fallen som
   förväntade, så att ett nytt DIFFERENT som bekräftas av körning fäller testet.
