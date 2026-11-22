#!/usr/bin/env python3
"""Svartlådetest för --flow (Bronze steg 1, docs/bronze.md).

Varje fall är en .cpp-fil eller en katalog (alla dess .cpp i samma körning,
som flera översättningsenheter). Ovanför varje funktion står det förväntade
flödet, en rad per utdata, i samma form som pork skriver det:

    // flow: return <- discounted, price
    // flow: audit <- price (always written)

Trust: `<fall>.trust` bredvid en fil eller `trust.txt` i en katalog. Testet
fallerar om en Stone-funktion saknar `// flow:`-rader, om raderna skiljer sig
från porks, eller om en förväntan står vid en funktion som pork inte
rapporterar.

    check_flow.py PORK DIR
    check_flow.py PORK DIR --oracle [--selftest] [--silver-prover=esbmc]

Med --oracle körs också beroendeoraklet (--silver --flow-oracle, docs/bronze.md
steg 3): ett HOLE är ett fel, och `// flow oracle: ok|untested` ovanför en
funktion kräver det utfallet (ok = alla delar prövade utan hål). Med
--selftest tappar pork ett beroende per utdata först; då ska varje funktion
med `// flow oracle: ok` och ett beroende utöver sig själv få ett hål.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

HEAD = re.compile(r"^(.*):(\d+):(\d+): (.+?): (silver|bronze|stone|not-stone|trusted)")
FLOW = re.compile(r"^\s*//\s*flow:\s*(.+?)\s*$")
ORACLE = re.compile(r"^\s*//\s*flow oracle:\s*(\S+)")
INCLUDE = Path(__file__).resolve().parent.parent / "include"


def expectations(f, pattern=FLOW):
    """Rad för funktionens deklaration -> förväntade rader (kommentarerna ovanför)."""
    lines = f.read_text().split("\n")
    out = {}
    for i, l in enumerate(lines):
        if l.strip().startswith("//") or not l.strip():
            continue
        j, got = i - 1, []
        while j >= 0 and lines[j].strip().startswith("//"):
            if m := pattern.match(lines[j]):
                got.insert(0, m.group(1))
            j -= 1
        if got:
            out[i + 1] = got
    return out


def inside(p, q):
    return p == q or (p.startswith(q) and len(p) > len(q) and p[len(q)] in ".[")


def droppable(line):
    """En flödesrad 'o <- a, b (...)' med ett beroende som inte är en del av o."""
    if " <- " not in line:
        return False
    out, deps = line.split(" <- ", 1)
    deps = deps.split(" (")[0]
    return deps != "nothing" and any(not inside(out, d) and not inside(d, out) for d in deps.split(", "))


def main():
    pork, root = sys.argv[1], Path(sys.argv[2])
    oracle = "--oracle" in sys.argv[3:]
    selftest = "--selftest" in sys.argv[3:]
    extra = [a for a in sys.argv[3:] if a.startswith("--silver-prover")]
    if oracle:
        extra += ["--silver", "--flow-oracle", "--no-silver-cache", *(["--flow-oracle-selftest"] if selftest else [])]
    totals = {"ok": 0, "untested": 0, "HOLE": 0}
    if oracle:
        prover = "esbmc" if any(a.endswith("=esbmc") for a in extra) else "cbmc"
        if shutil.which(prover) is None:
            print(f"flow oracle: skipped ({prover} not found)")
            return 0
    failures = cases = 0
    for case in sorted(root.iterdir()):
        files = sorted(case.glob("*.cpp")) if case.is_dir() else [case] if case.suffix == ".cpp" else []
        if not files:
            continue
        cases += 1
        trust = case / "trust.txt" if case.is_dir() else case.with_suffix(".trust")
        args = [pork, "--flow", *extra, *(["--trust", str(trust)] if trust.exists() else []), *map(str, files),
                "--", "-std=c++23", f"-I{case if case.is_dir() else case.parent}", f"-I{INCLUDE}"]
        proc = subprocess.run(args, capture_output=True, text=True)
        got, current = {}, None  # (fil, rad) -> rader
        for line in proc.stdout.splitlines():
            if m := HEAD.match(line):
                current = (Path(m.group(1)).resolve(), int(m.group(2)))
                got[current] = {"verdict": m.group(5), "name": m.group(4), "flow": [], "oracle": []}
            elif line.startswith("  flow: ") and current is not None:
                got[current]["flow"].append(line[len("  flow: "):])
            elif line.startswith("  flow oracle: ") and current is not None:
                got[current]["oracle"].append(line[len("  flow oracle: "):])
        if proc.returncode not in (0, 1) or not got:
            print(f"FAIL {case.name}: pork exit {proc.returncode}\n{proc.stderr[:2000]}")
            failures += 1
            continue
        bad = []
        if oracle:
            sources = files + (sorted(case.glob("*.hpp")) if case.is_dir() else [])
            wanted = {(f.resolve(), ln): e[0] for f in sources for ln, e in expectations(f, ORACLE).items()}
            for key, r in got.items():
                if not r["oracle"]:
                    continue
                outcome = ("HOLE" if any(o.startswith("HOLE") for o in r["oracle"]) else
                           "ok" if any(o.startswith("ok") for o in r["oracle"]) and
                           not any(o.startswith("untested") for o in r["oracle"]) else "untested")
                totals[outcome] += 1
                want = wanted.get(key)
                if selftest:
                    # Varje utdata med ett annat beroende än sig själv tappar ett:
                    # en funktion som ska vara ok ska då få ett hål.
                    if want == "ok" and any(droppable(l) for l in r["flow"]) and outcome != "HOLE":
                        bad.append(f"{r['name']}: self-test expected HOLE, got {r['oracle']}")
                elif outcome == "HOLE":
                    bad.append(f"{r['name']}: HOLE: {[o for o in r['oracle'] if o.startswith('HOLE')]}")
                elif want is not None and want != outcome:
                    bad.append(f"{r['name']}: oracle expected {want}, got {r['oracle']}")
            print(("FAIL " if bad else "PASS ") + case.name)
            for b in bad:
                print("    " + b)
            failures += bool(bad)
            continue
        sources = files + (sorted(case.glob("*.hpp")) if case.is_dir() else [])
        expected = {(f.resolve(), ln): e for f in sources for ln, e in expectations(f).items()}
        for key, r in got.items():
            want = expected.pop(key, None)
            if r["verdict"] in ("stone", "bronze", "silver"):
                if want is None:
                    bad.append(f"{r['name']}: no // flow: lines (pork: {r['flow']})")
                elif sorted(want) != sorted(r["flow"]):
                    bad.append(f"{r['name']}: expected {want}, got {r['flow']}")
            elif want is not None:
                bad.append(f"{r['name']}: {r['verdict']}, but has // flow: lines")
        for (f, ln), want in expected.items():
            bad.append(f"{f.name}:{ln}: // flow: lines, but pork reports no function there")
        print(("FAIL " if bad else "PASS ") + case.name)
        for b in bad:
            print("    " + b)
        failures += bool(bad)
    if oracle:
        print(f"flow oracle{' self-test' if selftest else ''}: {cases} cases, {failures} failed, "
              f"ok {totals['ok']}, untested {totals['untested']}, HOLE {totals['HOLE']}")
    else:
        print(f"flow: {cases} cases, {failures} failed")
    return 1 if failures or cases == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
