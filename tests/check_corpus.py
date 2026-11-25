#!/usr/bin/env python3
"""Acceptanstest: kör pork över corpus/ och jämför mot förväntningar i källan.

Varje funktion i korpusen ska ha en kommentar på raden ovanför sitt namn:

    // expect: stone
    // expect: not-stone
    // expect: not-stone: <delsträng som ska finnas i orsaken>

Flera besked på samma rad (template-instanser) anges med staplade
förväntningar som namnger funktionen:

    // expect: identity<int>: stone
    // expect: identity<Holder>: not-stone: <orsak>
    template <typename T> T identity(T x) { return x; }

En förväntning utan namn gäller alla funktioner på raden.

En förväntning för en viss bevisare ersätter den vanliga när korpusen körs
med den (`--silver-prover=esbmc`), för de fall där bevisarna hittar samma
fel men beskriver det olika:

    // expect: stone: not silver: floating-point division by zero
    // expect[esbmc]: stone: not silver: NaN on division

    // expect: stone                     inga antaganden tillåts
    // expect: stone: assuming <...>     delsträng av antagandelistan
    // expect: trusted                   pork::trusted, kroppen analyseras inte
    // expect: silver[: assuming <...>]  bevisat fri från körtidsfel (--silver)
    // expect: stone: not silver: <...>  Stone, men inte bevisat Silver
    // expect-violation[: namn]          pork::stone- eller PORK_DEPENDS-krav som bryts (error, exit 1);
                                      för ett krav utanför en funktionskropp: raden före kravet

Kontrakt för betrodda funktioner (Silver): `NN_namn.contracts.c` bredvid fallet
(`contracts.c` i en katalog), skickas som `--contracts`.

Extra argument till pork: `// pork-args: --silver` någonstans i filen. Fall
med --silver hoppas över (SKIP) om bevisaren inte finns på PATH.

Argument efter korpuskatalogen läggs till i Silver-fallen, t.ex.
`--silver-prover=esbmc` för att köra samma spec med ESBMC. Då körs bara
Silver-fallen.

Trust-fil: `<fall>.trust` bredvid en fil, eller `trust.txt` i en katalog.
Sammanfattningar (lånemodellen): `<fall>.summaries` bredvid en fil, eller
`summaries.txt` i en katalog, skickas som `--summaries` (ersätter rader i std.txt).

Ett fall är antingen en .cpp- eller .c-fil (C: -std=c17) eller en katalog (alla i den körs i
samma pork-anrop, för analys över flera översättningsenheter). I en katalog läses
förväntningar också i .hpp/.h (funktioner definierade i en header).

Testet fallerar om
  - en förväntning saknar motsvarande rad i utdata,
  - pork rapporterar en funktion som saknar förväntning,
  - beskedet eller orsaken inte stämmer,
  - förväntade och faktiska kravbrott (stderr) skiljer sig,
  - exit-koden inte är 1 när kravbrott förväntas, annars 0.
"""

import os
import re
import shutil
import subprocess
from functools import cache
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import sys
from pathlib import Path

EXPECT = re.compile(
    r"//\s*expect(?:\[(\w+)\])?:\s*(?:(\S+?):\s+)?(silver|bronze|stone|not-stone|trusted)(?::\s*(.+?))?\s*$")
EXPECT_VIOLATION = re.compile(r"//\s*expect-violation(?::\s*(\S+))?\s*$")
OUTPUT = re.compile(r"^(.*):(\d+):(\d+): (.+?): (silver|bronze|stone|not-stone|trusted)(?:: (.*))?$")
PORK_ARGS = re.compile(r"//\s*pork-args:\s*(.+?)\s*$")
RULES = re.compile(r"//\s*rules:\s*(.+?)\s*$")
LOANS = re.compile(r"//\s*loans:\s*(ok|rejected)(?::\s*(.+?))?\s*$")  # tests/check_loans.py
RULE_ID = re.compile(r"^((?:STN|SLV|BRZ)-\d+)\s+(.*)$")
VIOLATION = re.compile(r"^(.*):(\d+):(\d+): error: (?:'(.+?)' (?:is marked pork::stone|does not meet PORK_DEPENDS)"
                       r"|PORK_DEPENDS in .+ (?:is outside a function body|must come before all other statements))")


def expectations(files, prover="cbmc"):
    """Returnerar (exp, violations).
    exp: (fil, rad, namn eller None) -> (besked, detalj).
    violations: mängd av (fil, rad, namn eller None).
    Raden är den första raden under förväntningarna som inte själv är en."""
    exp, violations = {}, set()
    for f in files:
        pending, pending_v = [], []
        for n, line in enumerate(f.read_text().splitlines(), start=1):
            if m := EXPECT.search(line):
                pending.append(m)
                continue
            if m := EXPECT_VIOLATION.search(line):
                pending_v.append(m)
                continue
            if LOANS.search(line):
                continue  # lånemodellens förväntan, se check_loans.py
            # vanliga förväntningar först, så att den för bevisaren skriver över
            for p in sorted(pending, key=lambda p: p.group(1) is not None):
                if p.group(1) not in (None, prover):
                    continue
                exp[(f.resolve(), n, p.group(2))] = (p.group(3), p.group(4))
            for p in pending_v:
                violations.add((f.resolve(), n, p.group(1)))
            pending, pending_v = [], []
    return exp, violations


MARK = re.compile(r"\s*\[(allows|rejects|test:[^\]]*)\]")


@dataclass
class Rule:
    title: str
    polarity: str = "both"  # allows: kräver godkänt fall, rejects: underkänt, both: båda
    tests: list = field(default_factory=list)  # tester utanför korpusen (GoogleTest Svit.Namn, ctest-namn)


def load_rules(spec):
    """Regelkatalogen i spec/*.txt: ID -> Rule. Rubrikraden kan sluta med
    `[allows]`/`[rejects]` och `[test: A.B, ctest_namn]`. Borttagna regler har
    rubriken "(removed)"."""
    rules = {}
    for f in sorted(spec.glob("*.txt")):
        for line in f.read_text().splitlines():
            if m := RULE_ID.match(line):
                rule = Rule(MARK.sub("", m.group(2)).strip())
                for mark in MARK.findall(m.group(2)):
                    if mark.startswith("test:"):
                        rule.tests += [t.strip() for t in mark[5:].split(",") if t.strip()]
                    else:
                        rule.polarity = mark
                rules[m.group(1)] = rule
    return rules


def known_tests(repo):
    """GoogleTest-namn (Svit.Namn) under tests/ och ctest-namn i CMakeLists.txt."""
    names = set()
    for f in (repo / "tests").rglob("*.cpp"):
        names |= {f"{a}.{b}" for a, b in re.findall(r"^TEST(?:_F)?\((\w+),\s*(\w+)\)", f.read_text(), re.M)}
    names |= set(re.findall(r"add_test\(NAME\s+(\w+)", (repo / "CMakeLists.txt").read_text()))
    return names


def rule_tags(files):
    """Fallen och deras regler: lista av (fil, rad, regler, besked, detalj).
    `// rules: A, B` gäller förväntningarna i samma kommentarsblock, ovanför
    nästa rad som inte är en kommentar (funktionen)."""
    cases = []
    for f in files:
        rules, expects = [], []
        for n, line in enumerate(f.read_text().splitlines(), start=1):
            stripped = line.strip()
            if m := RULES.search(line):
                rules += [r.strip() for r in m.group(1).split(",") if r.strip()]
                continue
            if m := EXPECT.search(line):
                expects.append(m)
                continue
            if stripped.startswith("//"):
                continue
            for e in expects:
                cases.append((f, n, list(rules), e.group(3), e.group(4) or "", e.group(1)))
            rules, expects = [], []
    return cases


def check_rules(corpus, spec):
    """--rules: omärkta fall och okända/borttagna regel-ID:n är fel; skriver täckningen."""
    rules = load_rules(spec)
    errors, untagged = [], 0
    cover = {r: [0, 0, set()] for r in rules}  # godtas, underkänns, filer
    for case in sorted(corpus.iterdir()):
        files = case_files(case)
        if not files:
            continue
        for f, n, tags, verdict, detail, prover in rule_tags([*files, *header_files(case)]):
            if prover is not None:
                continue  # expect[esbmc]: samma fall som den vanliga raden
            if not tags:
                untagged += 1
                errors.append(f"{f.name}:{n}: case has no // rules: line")
            for t in tags:
                if t not in rules:
                    errors.append(f"{f.name}:{n}: unknown rule {t}")
                    continue
                if rules[t].title == "(removed)":
                    errors.append(f"{f.name}:{n}: removed rule {t}")
                    continue
                # Stone-regel: godtas = stone/bronze/silver/trusted. Silver-regel: godtas = silver.
                # Bronze-regel: godtas = bronze/silver (kraven håller).
                ok = (verdict == "silver" if t.startswith("SLV") else
                      verdict in ("bronze", "silver") if t.startswith("BRZ") else verdict != "not-stone")
                cover[t][0 if ok else 1] += 1
                cover[t][2].add(case.name)
    tests = known_tests(spec.parent)
    for r, rule in rules.items():
        for t in rule.tests:
            if t not in tests:
                errors.append(f"{r}: unknown test {t}")
    for r, (ok, bad, where) in cover.items():
        rule = rules[r]
        if rule.title == "(removed)":
            continue
        missing = []
        if rule.polarity in ("both", "allows") and not ok:
            missing.append("accepted")
        if rule.polarity in ("both", "rejects") and not bad:
            missing.append("rejected")
        if rule.tests:
            flag = ""  # testerna utanför korpusen täcker det som saknas här
        elif not ok and not bad:
            flag = "  <- untested"
        else:
            flag = "".join(f"  <- no {m} case" for m in missing)
        pol = {"both": "", "allows": "+", "rejects": "-"}[rule.polarity]
        extra = f"  [+{len(rule.tests)} test]" if rule.tests else ""
        print(f"{r:7}{pol:1} {ok:3} ok {bad:3} rejected  {rule.title[:48]:48}{extra}{flag}")
    print(f"untagged cases: {untagged}")
    for e in errors:
        print(f"ERROR {e}")
    return 1 if errors else 0


def trust_file(case):
    t = case / "trust.txt" if case.is_dir() else case.with_suffix(".trust")
    return ["--trust", str(t)] if t.exists() else []


def summaries_file(case):
    s = case / "summaries.txt" if case.is_dir() else case.with_suffix(".summaries")
    return ["--summaries", str(s)] if s.exists() else []


def contracts_file(case):
    c = case / "contracts.c" if case.is_dir() else case.with_suffix(".contracts.c")
    return ["--contracts", str(c)] if c.exists() else []


def extra_args(files):
    args = []
    for f in files:
        for line in f.read_text().splitlines():
            if m := PORK_ARGS.search(line):
                args += m.group(1).split()
    return args


def is_case_file(f):
    """Ett fall i C++ (.cpp) eller C (.c, men inte NN.contracts.c)."""
    return f.suffix == ".cpp" or (f.suffix == ".c" and not f.name.endswith(".contracts.c"))


def case_files(case):
    if case.is_dir():
        return sorted(f for f in case.iterdir() if is_case_file(f) and f.name != "contracts.c")
    return [case] if is_case_file(case) else []


def header_files(case):
    """Headers i ett katalogfall: kompileras inte själva, men funktioner som
    definieras i dem rapporteras på deras rader, så förväntningarna kan stå där."""
    return sorted(f for f in case.iterdir() if f.suffix in (".hpp", ".h")) if case.is_dir() else []


@cache
def memory_cap():
    """Minnestak för bevisaren (cbmc/esbmc): utan det dödar oomd terminalen.
    systemd-run --user kräver en användarsession, som saknas i containrar och CI;
    då körs bevisaren utan tak."""
    cap = ["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=8G", "-p", "MemorySwapMax=0"]
    if shutil.which("systemd-run") is None:
        return []
    probe = subprocess.run([*cap[:4], "true"], capture_output=True)
    return cap if probe.returncode == 0 else []


def run_case(pork, case, files, extra, prover):
    include = Path(__file__).resolve().parent.parent / "include"
    std = "-std=c17" if all(f.suffix == ".c" for f in files) else "-std=c++23"
    cmd = [pork, *extra, *trust_file(case), *summaries_file(case), *contracts_file(case), *map(str, files), "--", std,
           f"-I{include}"]
    if "--silver" in extra:
        cmd = [*memory_cap(), *cmd]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    exp, violations = expectations([*files, *header_files(case)], prover)
    want_rc = 1 if violations else 0
    if proc.returncode != want_rc:
        return [f"pork exited {proc.returncode}, expected {want_rc}:\n{proc.stderr}"]

    errors = []
    seen = set()
    for line in proc.stdout.splitlines():
        m = OUTPUT.match(line)
        if not m:
            errors.append(f"unparsable output: {line}")
            continue
        where = (Path(m.group(1)).resolve(), int(m.group(2)))
        key = (*where, m.group(4))
        if key not in exp:
            key = (*where, None)
        if key not in exp:
            errors.append(f"no expectation for: {line}")
            continue
        seen.add(key)
        verdict, reason = exp[key]
        actual = m.group(6)
        if m.group(5) != verdict:
            errors.append(f"expected {verdict}: {line}")
        elif verdict in ("stone", "bronze", "silver") and not reason and actual:
            errors.append(f"expected {verdict} without detail: {line}")
        elif reason and reason not in (actual or ""):
            errors.append(f"expected detail containing '{reason}': {line}")
    for key in exp.keys() - seen:
        name = f" ({key[2]})" if key[2] else ""
        errors.append(f"no output for expectation at {key[0]}:{key[1]}{name}")

    seen_v = set()
    for line in proc.stderr.splitlines():
        m = VIOLATION.match(line)
        if not m:
            continue
        where = (Path(m.group(1)).resolve(), int(m.group(2)))
        key = (*where, m.group(4))
        if key not in violations:
            key = (*where, None)
        if key not in violations:
            errors.append(f"unexpected violation: {line}")
            continue
        seen_v.add(key)
    for key in violations - seen_v:
        name = f" ({key[2]})" if key[2] else ""
        errors.append(f"no violation reported at {key[0]}:{key[1]}{name}")
    return errors


def main():
    if sys.argv[1] == "--rules":  # check_corpus.py --rules <korpus> <spec>
        return check_rules(Path(sys.argv[2]), Path(sys.argv[3]))
    pork, corpus, silver_args = sys.argv[1], Path(sys.argv[2]), sys.argv[3:]
    prover = "cbmc"
    for a in silver_args:
        if a.startswith("--silver-prover="):
            prover = a.split("=", 1)[1]
    failed = 0
    jobs = []  # (case, files, extra); Silver-fall (cbmc/esbmc) körs ett i taget: minnet
    for case in sorted(corpus.iterdir()):
        files = case_files(case)
        if not files:
            continue
        extra = extra_args(files)
        if silver_args and "--silver" not in extra:
            continue  # bara Silver beror på bevisaren; resten körs redan utan silver_args
        if "--silver" in extra and shutil.which(prover) is None:
            print(f"SKIP {case.name} ({prover} not on PATH)")
            continue
        if "--silver" in extra:
            # specen gäller bevisaren, inte cachen
            extra = extra + ["--no-silver-cache", *silver_args]
        jobs.append((case, files, extra))
    # Först fallen utan Silver parallellt; Silver-fallen sedan, ett i taget och
    # först när poolen är klar, så att bevisaren inte delar minnet med den.
    with ThreadPoolExecutor(max(1, (os.cpu_count() or 2) // 2)) as pool:
        results = dict(zip((j[0] for j in jobs if "--silver" not in j[2]),
                           pool.map(lambda j: run_case(pork, *j, prover), [j for j in jobs if "--silver" not in j[2]])))
    for case, files, extra in jobs:
        if "--silver" in extra:
            results[case] = run_case(pork, case, files, extra, prover)
    for case, _, _ in jobs:
        errors = results[case]
        print(f"{'FAIL' if errors else 'PASS'} {case.name}")
        for e in errors:
            print(f"    {e}")
        failed += bool(errors)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
