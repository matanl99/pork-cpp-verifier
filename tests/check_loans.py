#!/usr/bin/env python3
"""Lånemodellen bredvid Stone-reglerna (docs/stone_model.md, parallelldrift).

Kör pork med --loans=report över korpusen. Varje funktion får två domar:
reglerna (LocalCheck, "local") och lånemodellen ("loans").

Förväntningar på lånemodellen står i samma kommentarsblock som `// expect:`:

    // loans: ok
    // loans: rejected: <delsträng av orsaken>

Testet fallerar om en sådan förväntning inte stämmer. Oenigheter mellan
reglerna och modellen utan förväntan skrivs ut men fäller inte testet:

    only loans rejects  en sammanfattning saknas i modellen, eller ett hål i reglerna
    only local rejects  modellen godkänner (ofta: regeln gäller annat än lån)
    only loans rejects  reglerna godkänner: lån som bara modellen kontrollerar
                        (låneregler som tagits bort ur LocalCheck, steg 6 i
                        docs/stone_model.md); Stone avvisar ändå (enforce)

`--list` skriver ut varje oenighet.
"""

import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_corpus import LOANS, OUTPUT, case_files, extra_args, summaries_file, trust_file  # noqa: E402

LOANS_OUT = re.compile(r"^  loans: (ok|rejected: (.*)); local: (ok|rejected)$")


def loan_expectations(files):
    exp = {}
    for f in files:
        pending = []
        for n, line in enumerate(f.read_text().splitlines(), start=1):
            if m := LOANS.search(line):
                pending.append(m)
                continue
            if line.strip().startswith("//"):
                continue
            for m in pending:
                exp[(f.resolve(), n)] = (m.group(1), m.group(2))
            pending = []
    return exp


def main():
    args = [a for a in sys.argv[1:] if a != "--list"]
    listing = "--list" in sys.argv
    pork, corpus = args[0], Path(args[1])
    include = Path(__file__).resolve().parent.parent / "include"
    failed, counts, disagree = 0, {"agree": 0, "only-loans": 0, "only-local": 0}, []
    runs = []  # (case, files, kommando): pork körs parallellt, utdata granskas i ordning
    for case in sorted(corpus.iterdir()):
        files = case_files(case)
        if not files:
            continue
        extra, skip = [], False
        for a in extra_args(files):  # Silver-flaggor (och deras värden) behövs inte här
            if skip:
                skip = False
            elif a == "--vector-bound":
                skip = True
            elif not a.startswith("--silver") and not a.startswith("--vector-bound"):
                extra.append(a)
        std = "-std=c17" if all(f.suffix == ".c" for f in files) else "-std=c++23"
        runs.append((case, files, [pork, "--loans=report", *extra, *trust_file(case), *summaries_file(case),
                                   *map(str, files), "--", std, f"-I{include}"]))
    with ThreadPoolExecutor(max(1, (os.cpu_count() or 2) // 2)) as pool:
        procs = list(pool.map(lambda r: subprocess.run(r[2], capture_output=True, text=True), runs))
    for (case, files, _), proc in zip(runs, procs):
        errors = []
        if proc.returncode not in (0, 1):
            errors.append(f"pork exited {proc.returncode}:\n{proc.stderr}")
        exp = loan_expectations(files)
        seen = set()
        current = None
        for line in proc.stdout.splitlines():
            if m := OUTPUT.match(line):
                current = m
                continue
            m = LOANS_OUT.match(line)
            if not m or current is None:
                continue
            where = (Path(current.group(1)).resolve(), int(current.group(2)))
            loans_ok, local_ok = m.group(1) == "ok", m.group(3) == "ok"
            name = f"{case.name}:{where[1]} {current.group(4)}"
            if loans_ok == local_ok:
                counts["agree"] += 1
            elif local_ok:
                counts["only-loans"] += 1
                disagree.append(f"only loans rejects  {name}: {m.group(2)}")
            else:
                counts["only-local"] += 1
                disagree.append(f"only local rejects  {name}: {current.group(6)}")
            if where in exp:
                seen.add(where)
                want, detail = exp[where]
                if want == "ok" and not loans_ok:
                    errors.append(f"expected loans: ok: {name}: {m.group(2)}")
                elif want == "rejected" and loans_ok:
                    errors.append(f"expected loans: rejected: {name}")
                elif want == "rejected" and detail and detail not in m.group(2):
                    errors.append(f"expected loans detail '{detail}': {name}: {m.group(2)}")
        for where in exp.keys() - seen:
            errors.append(f"no loans verdict for expectation at {where[0].name}:{where[1]}")
        if errors or exp:
            print(f"{'FAIL' if errors else 'PASS'} {case.name}")
        for e in errors:
            print(f"    {e}")
        failed += bool(errors)
    print(f"agree {counts['agree']}, only loans rejects {counts['only-loans']}, "
          f"only local rejects {counts['only-local']}")
    if listing:
        for d in disagree:
            print(f"  {d}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
