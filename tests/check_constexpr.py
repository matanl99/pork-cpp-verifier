#!/usr/bin/env python3
"""Constexpr-orakel för korpusen (komplement till körningsoraklet, P-e).

Ett korpusfall med prov (`#ifdef PORK_CONSTEXPR`, include/pork/constexpr.hpp)
kopieras, `constexpr` sätts på varje funktion som pork rapporterar i filen,
och kopian kompileras med -DPORK_CONSTEXPR -D_GLIBCXX_ASSERTIONS. Varje prov
är ett static_assert: kompilatorn måste utvärdera anropet och avvisa
odefinierat beteende i kärnspråket ([expr.const]/5). Det ser sådant som ASan
inte ser: råa pekare i olika objekt (std::array), läsning efter livstidens
slut, oinitierade värden.

Orsaken är den första noten efter felet. Den sorteras:

    stone    livstid, oinitierat, pekare i olika objekt, inaktiv unionsmedlem
             (std::variant efter tilldelning), okänd orsak,
             fel resultat (static_assert false): det Stone lovar (G2-G4)
    runtime  overflow, division med noll, index utanför, nullpekare,
             bibliotekets förvillkor (_GLIBCXX_ASSERTIONS): körtidsfel, det
             Silver lovar, inte Stone
    cannot   provet går inte att utvärdera (icke-constexpr-anrop, global,
             statisk lokal, gräns för antal steg): fäller testet, provet prövar inget

Domen kommer från pork med fallets egna argument utom --silver (Silver körs
med minnestak i check_corpus; här jämförs bara Stone):

    HOLE      pork: stone, orsak stone                   (fäller testet)
    caught    pork: not-stone, provet avvisas
    runtime   pork: stone, orsak runtime                 (Stone lovar inte det)
    quiet     pork: not-stone, provet går igenom
    ok        pork: stone, provet går igenom

Namnen följer körningsoraklet (`namn@rad`, `namn#n`); ett namn som pork inte
rapporterar fäller testet, liksom en kopia som inte kompilerar av andra skäl.

    check_constexpr.py PORK CORPUS [--jobs N] [--summaries FIL] [--list]

Clang 23 kan hänga på ett prov (string_view till en temporär std::string,
korpus 61): en kompilering som inte blir klar på 30 s görs om med ett prov i
taget, och ett prov som hänger är `untested` (en körning) eller ett fel
(ett PORK_CONSTEXPR_CASE).
"""

import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_corpus import OUTPUT, case_files, extra_args, summaries_file, trust_file  # noqa: E402
from check_runtime import resolve  # noqa: E402

INCLUDE = Path(__file__).resolve().parent.parent / "include"
CASE = re.compile(r'PORK_CONSTEXPR_CASE\(\s*"([^"]+)"')
RUN = re.compile(r'\{\s*"([^"]+)"\s*,\s*\[')  # en körning i PORK_RUN_CASES
DIAG = re.compile(r"^(.*?):(\d+):(\d+): (error|note|warning): (.*)$")
# Fel i kopian: rad i filen och meddelande.
STONE = re.compile(r"has been deleted|outside its lifetime|lifetime has ended|uninitialized|unrelated|"
                   r"different (arrays|objects)|past the end of a complete object|const-qualified|dangling|"
                   r"temporary|union with active member|delete of")
RUNTIME = re.compile(r"outside the range of representable|division by zero|remainder by zero|"
                     r"one-past-the-end|cannot refer to element|null pointer|shift|out of bounds|"
                     r"__glibcxx_assert_fail|outside the valid range")
CANNOT = re.compile(r"non-constexpr|non-const variable|not allowed in a constant expression|non-literal|"
                    r"cannot be used in a constant expression|maximum|undefined function|virtual base|"
                    r"reinterpret_cast|volatile|inline assembly|visible outside that expression|"
                    r"subexpression not valid in a constant expression|"  # t.ex. throw (C++23)
                    r"definition of a (static|thread_local) variable")  # statisk lokal (konstant tabell)
SKIP_NOTE = re.compile(r"^(expanded from|in call to|declared here|in instantiation|while )")


def verdicts(pork, case, files, summaries):
    sm = ["--summaries", summaries] if summaries else summaries_file(case)
    args = [a for a in extra_args(files) if not a.startswith("--silver")]
    proc = subprocess.run([pork, *args, *trust_file(case), *sm, *map(str, files), "--",
                           "-std=c++23", f"-I{INCLUDE}"], capture_output=True, text=True)
    out, where = {}, {}  # namn -> [(rad, stone)]; fil -> {(rad, kolumn)}
    for line in proc.stdout.splitlines():
        if m := OUTPUT.match(line):
            out.setdefault(m.group(4), []).append((int(m.group(2)), m.group(5) in ("stone", "silver")))
            where.setdefault(Path(m.group(1)).resolve(), set()).add((int(m.group(2)), int(m.group(3))))
    return out, where


DECL_CHARS = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:<>,*&~ \t")


def with_constexpr(text, places):
    """Kopian: `constexpr` före varje rapporterad funktions deklaration.

    Deklarationen börjar efter närmaste `;`, `{`, `}`, `]` (ett attribut) eller
    radens början före namnet, efter en åtkomstetikett och ett template-huvud.
    En felplacering ger ett kompileringsfel (ERROR), aldrig ett godkänt prov."""
    lines = text.split("\n")
    for line, col in sorted(places, reverse=True):
        s = lines[line - 1]
        start = col - 1
        while start > 0 and s[start - 1] in DECL_CHARS:
            start -= 1
        seg = s[start:col - 1]
        if re.search(r"\b(constexpr|consteval)\b", seg):
            continue
        skip = re.match(r"\s*(?:(?:public|private|protected)\s*:(?!:)\s*)*(?:template\s*<.*>\s*)?", seg)
        at = start + skip.end()
        lines[line - 1] = s[:at] + "constexpr " + s[at:]
    return "\n".join(lines)


def classify(notes):
    if not notes:
        return "stone", "error without a note"
    if "static assertion failed" in notes[0]:
        return "stone", "wrong result"
    cause = next((n for n in notes[1:] if not SKIP_NOTE.match(n)), notes[0])
    # Livstid först: "read of object outside its lifetime is not allowed in a
    # constant expression" är ett fel, ingen begränsning.
    for kind, pattern in (("stone", STONE), ("runtime", RUNTIME), ("cannot", CANNOT)):
        if pattern.search(cause):
            return kind, cause
    return "stone", cause  # okänd orsak: som ett larm


CLANG = os.environ.get("PORK_CONSTEXPR_CLANG", "clang++")  # självtestet byter ut den
FLAGS = ["-std=c++23", "-fsyntax-only", "-ferror-limit=0", "-Wno-invalid-constexpr", "-w", "-DPORK_RUN",
         "-DPORK_RUN_AS_CONSTEXPR", "-D_GLIBCXX_ASSERTIONS", f"-I{INCLUDE}"]
NON_CONSTEXPR = re.compile(r"^non-constexpr (function|constructor) ")
WHOLE, SINGLE = 30, 20  # sekunder; clang 23 kan hänga (string_view till en temporär std::string)


def compile_copy(copy, text, flags, timeout):
    """(fel och noter per rad i kopian, platser för icke-constexpr-funktioner i
    kopian), eller None när kompilatorn inte blev klar."""
    copy.write_text(text)
    try:
        proc = subprocess.run([CLANG, *FLAGS, *flags, str(copy)], capture_output=True, text=True,
                              timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    diags, current = [], None  # [(rad eller None, [meddelanden], rad i utdata)]
    missing, pending = set(), False  # icke-constexpr-funktioner i kopian (hjälpfunktioner i PORK_RUN)
    for line in proc.stderr.splitlines():
        m = DIAG.match(line)
        if m is None:
            continue
        here = Path(m.group(1)).resolve() == copy.resolve()
        if m.group(4) == "error":
            current = (int(m.group(2)) if here else None, [m.group(5)], line.strip())
            diags.append(current)
            pending = False
        elif m.group(4) == "note" and current is not None:
            current[1].append(m.group(5))
            if NON_CONSTEXPR.search(m.group(5)):
                pending = True
            elif pending and m.group(5).startswith("declared here"):
                pending = False
                if here:
                    missing.add((int(m.group(2)), int(m.group(3))))
    # Ett misslyckat kompilatoranrop utan fel att tolka (kraschad, dödad,
    # saknas) är ett verktygsfel, aldrig ett godkänt prov.
    if proc.returncode != 0 and not diags:
        raise RuntimeError(f"clang++ exited with {proc.returncode} without a diagnostic: "
                           f"{proc.stderr.strip()[-500:]}")
    return diags, missing


def declared_constexpr(f, text, places, copy):
    """with_constexpr, också på tidigare deklarationer: clang pekar ut dem
    ("constexpr declaration of 'f' follows non-constexpr declaration")."""
    for _ in range(5):
        base = with_constexpr(text, places)
        copy.write_text(base)
        try:
            proc = subprocess.run([CLANG, *FLAGS, str(copy)], capture_output=True, text=True, timeout=WHOLE)
        except subprocess.TimeoutExpired:
            return base
        found, follows = set(), False
        for line in proc.stderr.splitlines():
            m = DIAG.match(line)
            if m is None:
                continue
            if m.group(4) == "error":
                follows = "follows non-constexpr declaration" in m.group(5)
            elif follows and m.group(4) == "note" and m.group(5).startswith("previous declaration is here") and \
                    Path(m.group(1)).resolve() == copy.resolve():
                found.add((int(m.group(2)), int(m.group(3))))
        if not found - places:
            return base
        places |= found
    return base


def check_file(f, places, tmp):
    """(prov, fel): prov = {rad: (namn, återanvänd, sort, orsak)}, fel = fel utanför proven."""
    text = f.read_text()
    probes = {i + 1: (m.group(1), False) for i, l in enumerate(text.split("\n")) if (m := CASE.search(l))}
    marked = set(places.get(f.resolve(), set()))
    base = declared_constexpr(f, text, marked, Path(tmp) / f.name)
    first = base.count("\n") + 2
    runs = RUN.findall(text[text.find("PORK_RUN_CASES("):]) if "PORK_RUN_CASES(" in text else []
    for k, name in enumerate(runs):
        probes[first + k] = (name, True)
    tail = lambda ks: "".join(f"\nstatic_assert(::pork_run::cases[{k}].run() == 0);" for k in ks)
    copy = Path(tmp) / f.name
    results, errors = {}, []

    def take(diags, rows):
        for row, msgs, line in diags:
            if row in rows:
                results[row] = classify(msgs)
            else:
                errors.append(f"{f.name}: {line}")

    explicit = {r for r, p in probes.items() if not p[1]}
    for _ in range(5):  # constexpr också på hjälpfunktioner som clang pekar ut
        got = compile_copy(copy, base + tail(range(len(runs))), ["-DPORK_CONSTEXPR"], WHOLE)
        diags = None if got is None else got[0]
        if got is None or not got[1] - marked:
            break
        marked |= got[1]
        base = declared_constexpr(f, text, marked, copy)
    if diags is not None:
        take(diags, set(probes))
    else:  # ett prov hänger: ett i taget
        got = compile_copy(copy, base, ["-DPORK_CONSTEXPR"], WHOLE) if explicit else ([], set())
        diags = None if got is None else got[0]
        if diags is None:
            results.update({r: ("cannot", "compiler timeout") for r in explicit})
        else:
            take(diags, explicit)
        for k in range(len(runs)):
            got = compile_copy(copy, base + tail([k]), [], SINGLE)
            diags = None if got is None else got[0]
            if diags is None:
                results[first + k] = ("cannot", "compiler timeout")
            else:  # provet står på raden `first` i den här kopian
                take([(first + k if r == first else r, m, l) for r, m, l in diags], {first + k})
    return {r: (*p, *results.get(r, (None, ""))) for r, p in probes.items()}, errors


def judge(pork, case, summaries, tmp):
    """Rader att skriva och räknare för ett fall."""
    files = case_files(case)
    ver, places = verdicts(pork, case, files, summaries)
    out, counts, errors = [], {}, 0
    for f in files:
        t = f.read_text()
        if "PORK_CONSTEXPR" not in t and "PORK_RUN_CASES" not in t:
            continue
        sub = Path(tmp) / case.stem
        sub.mkdir(exist_ok=True)
        try:
            probes, errs = check_file(f, places, sub)
        except RuntimeError as e:
            out.append((True, f"ERROR {case.name}: {e}"))
            errors += 1
            continue
        for e in errs:
            out.append((True, f"ERROR {case.name}: the copy does not compile: {e}"))
        errors += len(errs)
        if not probes:
            out.append((True, f"ERROR {case.name}: no PORK_CONSTEXPR_CASE or PORK_RUN_CASES"))
            errors += 1
        for row, (name, reused, kind, cause) in sorted(probes.items()):
            stone, err = resolve(ver, name)
            if err:
                out.append((True, f"ERROR {case.name} {name}: {err}"))
                errors += 1
                continue
            if kind == "cannot" and not reused:
                out.append((True, f"ERROR {case.name} {name}: cannot evaluate: {cause}"))
                errors += 1
                continue
            if kind == "cannot":
                verdict = "untested"
            elif kind is None:
                verdict = "ok" if stone else "quiet"
            elif kind == "runtime":
                verdict = "runtime" if stone else "caught"
            else:
                verdict = "HOLE" if stone else "caught"
            counts[verdict] = counts.get(verdict, 0) + 1
            out.append((verdict == "HOLE", f"{verdict} {case.name} {name}" + (f": {cause}" if kind else "")))
    return out, counts, errors


def main():
    args = sys.argv[1:]
    jobs, summaries, listing = 4, None, "--list" in args
    args = [a for a in args if a != "--list"]
    if "--jobs" in args:
        i = args.index("--jobs"); jobs = int(args[i + 1]); del args[i:i + 2]
    if "--summaries" in args:
        i = args.index("--summaries"); summaries = args[i + 1]; del args[i:i + 2]
    pork, corpus = args[0], Path(args[1])
    cases = [c for c in sorted(corpus.iterdir()) if (fs := case_files(c)) and
             any("PORK_CONSTEXPR" in (t := f.read_text()) or "PORK_RUN_CASES" in t for f in fs)]
    counts = {"HOLE": 0, "caught": 0, "runtime": 0, "quiet": 0, "ok": 0, "untested": 0}
    errors = 0
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(jobs) as pool:
        for out, c, e in pool.map(lambda case: judge(pork, case, summaries, tmp), cases):
            for always, line in out:
                if always or listing:
                    print(line)
            for k, v in c.items():
                counts[k] += v
            errors += e
    print(f"constexpr oracle: {len(cases)} cases, " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    return 1 if counts["HOLE"] or errors else 0


if __name__ == "__main__":
    sys.exit(main())
