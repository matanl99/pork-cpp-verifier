# Korpusen

Korpusen är porks spec i körbar form: svartlådetest som körs av
`tests/check_corpus.py` (ctest: `corpus`, `corpus_esbmc`, `corpus_rules`).
Reglerna själva står i `spec/1_stone.txt` (STN-n) och `spec/2_silver.txt`
(SLV-n).

## Struktur

En fil per **område**, inte en fil per regel. Ett område (lokala referenser,
sidoeffekter, std::vector i Silver, ...) har många fall, både sådana som ska
godtas och gränsfall som ska underkännas. En regel kan testas i flera filer.

- `NN_namn.cpp`: fallen. `04_multi/` är en katalog när fallen behöver flera TU:er.
- `NN_namn.trust`: trust-fil för fallet (`--trust`).
- `NN_namn.contracts.c`: kontrakt för betrodda funktioner (`--contracts`).
- `// pork-args: ...` någonstans i filen: flaggor för hela filen (t.ex. `--silver`).
- `30_gaps.cpp`: kända luckor med dagens dom. När en lucka stängs
  flyttas fallet till rätt fil och domen ändras.

## Ett fall

```cpp
// rules: STN-62
// expect: not-stone: modifies 't' while reference 'p' into it is live
void used_after_branch(Team& t, bool b) { ... }
```

- `// expect: <dom>[: <delsträng av orsaken>]` på raden ovanför funktionen.
  Staplade `// expect: namn<T>: ...` för flera template-instanser på samma rad.
  `// expect[esbmc]: ...` när esbmc formulerar fyndet annorlunda.
- `// rules: STN-n, SLV-n` i samma kommentarsblock: reglerna som fallet testar.
  Varje fall måste ha en; omärkta fall och okända eller borttagna ID:n ger fel
  (`corpus_rules`).

## Täckning

```sh
python3 -I tests/check_corpus.py --rules corpus spec
```

skriver per regel hur många fall som godtas (`ok`: stone/silver/trusted för en
STN-regel, silver för en SLV-regel) och underkänns, och flaggar det som saknas.
En regel kräver båda sidor, utom när rubriken i spec/ slutar med `[allows]`
(bara godkända fall krävs, `+` i rapporten) eller `[rejects]` (bara underkända,
`-`). `[test: Svit.Namn]` pekar på GoogleTest- eller ctest-tester för det som
korpusen inte kan uttrycka (slutstatus, bevisarfel); okända testnamn är fel.
