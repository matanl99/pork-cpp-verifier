#!/usr/bin/env python3
"""Körningsorakel för korpusen (P-e i docs/stone_model.md).

Ett korpusfall med körningar (`#ifdef PORK_RUN`, include/pork/run.hpp)
kompileras med -DPORK_RUN i två lägen och varje körning körs för sig:

    asan  -fsanitize=address,undefined -D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS
    msan  -fsanitize=memory (bara fall utan std-headers: libstdc++ är inte
          instrumenterad och ger falska larm)

En körning prövar en funktion: körningens namn utan `#n` (en andra körning
av samma funktion), `namn@rad` när flera funktioner har samma namn
(överlagringar, raden där pork rapporterar funktionen). Ett namn som passar
flera funktioner är ett fel: en underkänd överlagring får inte dölja ett hål
i en godkänd. Domen kommer från pork med fallets egna argument (trust,
summaries, pork-args):

    HOLE      pork: stone/silver, körningen larmar      (fäller testet)
    caught    pork: not-stone, körningen larmar
    quiet     pork: not-stone, körningen larmar inte   (inget bevis åt något håll;
              fäller aldrig testet, `--list` visar dem)
    ok        pork: stone/silver, körningen är tyst

Ett namn som pork inte rapporterar fäller också testet (felstavat fall), liksom
ett program som inte går att lista (kraschar) eller inte har några körningar.

    check_runtime.py PORK CORPUS [--jobs N] [--summaries FIL] [--list]

--summaries ersätter fallens sammanfattningar (självtestet: en felaktig rad
ska ge HOLE).
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

INCLUDE = Path(__file__).resolve().parent.parent / "include"
MODES = {
    "asan": ["-O0", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-D_GLIBCXX_DEBUG",
             "-D_GLIBCXX_ASSERTIONS"],
    "msan": ["-O0", "-fsanitize=memory", "-fsanitize-memory-track-origins"],  # -O1 gör grenar till val
}
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="print_stacktrace=1")


def verdicts(pork, case, files, summaries):
    sm = ["--summaries", summaries] if summaries else summaries_file(case)
    proc = subprocess.run([pork, *extra_args(files), *trust_file(case), *sm, *map(str, files), "--",
                           "-std=c++23", f"-I{INCLUDE}"], capture_output=True, text=True)
    out = {}  # namn -> [(rad, stone)]
    for line in proc.stdout.splitlines():
        if m := OUTPUT.match(line):
            out.setdefault(m.group(4), []).append((int(m.group(2)), m.group(5) in ("stone", "silver")))
    return out


def resolve(ver, name):
    """Domen för körningen `name` (namn, namn@rad, ...#n): (stone, fel)."""
    fn = name.split("#")[0]
    fn, _, line = fn.partition("@")
    found = ver.get(fn, [])
    if line:
        found = [f for f in found if f[0] == int(line)]
    if not found:
        return None, f"pork reports no function '{name.split('#')[0]}'"
    if len({f[0] for f in found}) > 1:
        lines = ", ".join(str(f[0]) for f in sorted(set(found)))
        return None, f"'{fn}' names functions on lines {lines}: use {fn}@<line>"
    # Samma rad: instanser av en mall. Stone bara om alla är det.
    return all(f[1] for f in found), None


def uses_std(files):
    return any(re.search(r"#\s*include\s*<(?!pork/)", f.read_text()) for f in files)


def build(files, mode, exe):
    proc = subprocess.run(["clang++", "-std=c++23", "-g", "-w", "-DPORK_RUN", f"-I{INCLUDE}", *MODES[mode],
                           *map(str, files), "-o", exe], capture_output=True, text=True)
    return proc.returncode == 0, proc.stderr


def run(exe, name):
    try:
        p = subprocess.run([exe, name], capture_output=True, text=True, timeout=10, env=ENV)
    except subprocess.TimeoutExpired:
        return True, "timeout"
    err = p.stderr
    alarm = p.returncode != 0 or "runtime error" in err or "Sanitizer" in err or "Error:" in err
    first = next((l for l in err.splitlines() if "ERROR" in l or "WARNING" in l or "runtime error" in l
                  or "Error:" in l), f"exit {p.returncode}")
    return alarm, first.strip()


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
             any("PORK_RUN" in f.read_text() for f in fs)]
    counts = {"HOLE": 0, "caught": 0, "quiet": 0, "ok": 0}
    errors = 0
    with tempfile.TemporaryDirectory() as tmp, ThreadPoolExecutor(jobs) as pool:
        for case in cases:
            files = case_files(case)
            ver = verdicts(pork, case, files, summaries)
            modes = ["asan"] + ([] if uses_std(files) else ["msan"])
            for mode in modes:
                exe = str(Path(tmp) / f"{case.stem}.{mode}")
                ok, err = build(files, mode, exe)
                if not ok:
                    print(f"ERROR {case.name} [{mode}]: does not compile\n{err[:2000]}")
                    errors += 1
                    continue
                listing_proc = subprocess.run([exe], capture_output=True, text=True, env=ENV)
                names = listing_proc.stdout.split()
                if listing_proc.returncode != 0 or not names:
                    print(f"ERROR {case.name} [{mode}]: listing the runs failed (exit {listing_proc.returncode}, "
                          f"{len(names)} runs)\n{listing_proc.stderr[:2000]}")
                    errors += 1
                    continue
                results = list(pool.map(lambda n: (n, *run(exe, n)), names))
                for name, alarm, what in results:
                    stone, err = resolve(ver, name)
                    if err:
                        print(f"ERROR {case.name} [{mode}] {name}: {err}")
                        errors += 1
                        continue
                    kind = ("HOLE" if alarm else "ok") if stone else ("caught" if alarm else "quiet")
                    counts[kind] += 1
                    if kind == "HOLE" or listing:
                        print(f"{kind} {case.name} [{mode}] {name}" + (f": {what}" if alarm else ""))
    print(f"runtime oracle: {len(cases)} cases, " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    return 1 if counts["HOLE"] or errors else 0


if __name__ == "__main__":
    sys.exit(main())
