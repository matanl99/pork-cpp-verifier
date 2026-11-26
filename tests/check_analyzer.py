#!/usr/bin/env python3
"""Korsorakel mot Clang Static Analyzer (CSA).

CSA hittar troliga buggar längs körvägar och tystnar vid osäkerhet; pork
ger en dom per funktion och avvisar vid osäkerhet. En CSA-varning vars
körväg bara går genom funktioner som pork godkänner är därför ett
kandidathål, som ett ASan-larm i check_runtime.py:

    HOLE       varje funktion på vägen är stone/bronze/silver/trusted
    explained  någon funktion på vägen är not-stone
    runtime    ett aritmetik- eller indexfel (division med noll, utanför en
               array) där inte varje funktion är silver: Silvers sak, inget
               hål i Stone
    false      CSA har fel, enligt en rad `// analyzer: <check>: <varför>`
               på raden ovanför varningen (som `// expect:`)
    unknown    en plats på vägen ligger inte i någon funktion pork rapporterar

`deadcode.*` räknas inte (en död skrivning är inget körtidsfel). Varningar i
headers utanför filen räknas inte heller. Funktionen för en plats är den
närmast föregående som pork rapporterar i samma fil (CSA:s `issue_context`
kontrollerar namnet för själva varningen).

    check_analyzer.py PORK DIR... [--list] [--jobs N] [-- KOMPILATORFLAGGOR]
    check_analyzer.py PORK --db BUILD/compile_commands.json [--trust FIL] [--list] [--jobs N]

Första formen: varje .cpp under DIR analyseras för sig med fallets egna
argument (NN.trust, NN.summaries, pork-args). Andra formen: ett repo som det
byggs. pork körs en gång över alla översättningsenheter (`-p`), CSA per post
med postens flaggor, och bara varningar och vägar i repots egna filer räknas
(under repots rot, utanför byggkataloger). Utan `--list` skrivs bara HOLE och
summan; exitkod 1 om något HOLE finns.
"""

import json
import os
import plistlib
import re
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_corpus import OUTPUT, extra_args, summaries_file, trust_file  # noqa: E402

INCLUDE = Path(__file__).resolve().parent.parent / "include"
APPROVED = ("stone", "bronze", "silver", "trusted")
IGNORED = ("deadcode.",)
RUNTIME = ("core.DivideZero", "security.ArrayBound", "alpha.security.ArrayBound", "core.BitwiseShift")
FALSE = re.compile(r"//\s*analyzer:\s*([\w.]+):\s*\S")


def pork_functions(pork, f, flags):
    """[(rad, namn, dom)] för funktioner som pork rapporterar i filen f."""
    proc = subprocess.run([pork, *extra_args([f]), *trust_file(f), *summaries_file(f), str(f), "--",
                           "-std=c++23", f"-I{INCLUDE}", *flags], capture_output=True, text=True)
    out = []
    for line in proc.stdout.splitlines():
        if (m := OUTPUT.match(line)) and Path(m.group(1)).resolve() == f.resolve():
            out.append((int(m.group(2)), m.group(4), m.group(5)))
    return sorted(out)


def csa_warnings(f, flags):
    with tempfile.TemporaryDirectory() as d:
        plist = Path(d) / "out.plist"
        subprocess.run(["clang++", "--analyze", "-Xanalyzer", "-analyzer-output=plist", "-std=c++23",
                        f"-I{INCLUDE}", *flags, str(f), "-o", str(plist)], capture_output=True, text=True)
        if not plist.exists():
            return None
        data = plistlib.loads(plist.read_bytes())
    files = data.get("files", [])
    main = [i for i, name in enumerate(files) if Path(name).resolve() == f.resolve()]
    return data.get("diagnostics", []), set(main)


def enclosing(funcs, line):
    before = [fn for fn in funcs if fn[0] <= line]
    return before[-1] if before else None


def short(name):
    return name.split("<")[0].split("::")[-1]


def check(pork, f, flags):
    funcs = {f.resolve(): pork_functions(pork, f, flags)}
    got = csa_warnings(f, flags)
    if got is None:
        return [(f, "error", "clang --analyze gave no output", [])]
    diags, main = got
    files = [f.resolve() if i in main else None for i in range(max(main, default=-1) + 1)]
    return classify(diags, files, funcs)


def classify(diags, files, funcs):
    """files: plist-filindex -> Path i repot eller None; funcs: Path -> [(rad, namn, dom)]."""
    def own(loc):
        i = loc["file"]
        return files[i] if i < len(files) else None

    results = []
    for d in diags:
        name = d["check_name"]
        where = own(d["location"])
        if name.startswith(IGNORED) or where is None:
            continue
        locs = [p["location"] for p in d["path"] if p["kind"] == "event" and own(p["location"]) is not None]
        locs.append(d["location"])
        on_path = []
        unknown = False
        for loc in locs:
            fn = enclosing(funcs.get(own(loc), []), loc["line"])
            if fn is None:
                unknown = True
            elif (own(loc), fn) not in on_path:
                on_path.append((own(loc), fn))
        bug = enclosing(funcs.get(where, []), d["location"]["line"])
        if bug is not None and d.get("issue_context") and short(bug[1]) != d["issue_context"]:
            unknown = True  # platsen ligger i något pork inte rapporterar (lambda, lokal klass ...)
        on_path = [fn for _, fn in on_path]
        line = d["location"]["line"]
        source = where.read_text(errors="replace").splitlines()
        above = source[line - 2] if 2 <= line <= len(source) + 1 else ""
        if (m := FALSE.search(above)) and m.group(1) == name:
            kind = "false"
        elif unknown:
            kind = "unknown"
        elif name.startswith(RUNTIME) and not all(fn[2] == "silver" for fn in on_path):
            kind = "runtime"
        elif all(fn[2] in APPROVED for fn in on_path):
            kind = "HOLE"
        else:
            kind = "explained"
        text = f"{where}:{line}: {name}: {d['description']}"
        results.append((where, kind, text, on_path))
    return results


def repo_root(db):
    d = Path(os.path.abspath(db)).parent  # inte resolve: compile_commands.json kan vara en länk in i build/
    return d.parent if d.name.startswith("build") else d


def in_repo(path, root):
    try:
        rel = path.resolve().relative_to(root)
    except ValueError:
        return False
    return not any(part.startswith("build") or part == "_deps" for part in rel.parts)


def repo_pork(pork, db, trust, root):
    entries = [e for e in json.loads(db.read_text()) if in_repo(Path(e["directory"], e["file"]), root)]
    sources = sorted({str(Path(e["directory"], e["file"]).resolve()) for e in entries})
    proc = subprocess.run([pork, "-p", str(db.parent), *(["--trust", trust] if trust else []), *sources],
                          capture_output=True, text=True)
    funcs = {}
    for line in proc.stdout.splitlines():
        if (m := OUTPUT.match(line)) and in_repo(Path(m.group(1)), root):
            funcs.setdefault(Path(m.group(1)).resolve(), set()).add((int(m.group(2)), m.group(4), m.group(5)))
    return {k: sorted(v) for k, v in funcs.items()}, entries


def entry_warnings(entry, root):
    argv = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    # --analyze finns bara i clang; en g++-post analyseras med clang och samma flaggor.
    out, skip = ["clang" if entry["file"].endswith(".c") else "clang++"], False
    for a in argv[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-Werror", "-MD", "-MMD") or a.startswith("-Werror="):
            continue
        out.append(a)
    with tempfile.TemporaryDirectory() as d:
        plist = Path(d) / "out.plist"
        subprocess.run([*out, "--analyze", "-Xanalyzer", "-analyzer-output=plist", "-o", str(plist)],
                       cwd=entry["directory"], capture_output=True, text=True)
        if not plist.exists():
            return None
        data = plistlib.loads(plist.read_bytes())
    files = [Path(entry["directory"], n).resolve() if in_repo(Path(entry["directory"], n), root) else None
             for n in data.get("files", [])]
    return data.get("diagnostics", []), files


def repo_check(pork, db, trust, jobs):
    root = repo_root(db)
    funcs, entries = repo_pork(pork, db, trust, root)
    with ThreadPoolExecutor(jobs) as ex:
        got = list(ex.map(lambda e: entry_warnings(e, root), entries))
    results, seen = [], set()
    for e, g in zip(entries, got):
        if g is None:
            results.append((Path(e["file"]), "error", f"{e['file']}: clang --analyze gave no output", []))
            continue
        for r in classify(g[0], g[1], funcs):
            if r[2] not in seen:  # samma header i flera översättningsenheter
                seen.add(r[2])
                results.append(r)
    return results, len(entries)


def main():
    args = sys.argv[1:]
    flags = []
    if "--" in args:
        i = args.index("--"); flags = args[i + 1:]; args = args[:i]
    listing = "--list" in args
    args = [a for a in args if a != "--list"]
    jobs = 4
    if "--jobs" in args:
        i = args.index("--jobs"); jobs = int(args[i + 1]); del args[i:i + 2]
    db = trust = None
    if "--db" in args:
        i = args.index("--db"); db = Path(args[i + 1]); del args[i:i + 2]
    if "--trust" in args:
        i = args.index("--trust"); trust = args[i + 1]; del args[i:i + 2]
    pork, dirs = args[0], args[1:]
    if db is not None:
        all_results, n = repo_check(pork, db, trust, jobs)
        files = range(n)
    else:
        files = sorted(f for d in dirs for f in Path(d).rglob("*.cpp") if "build" not in f.parts)
        with ThreadPoolExecutor(jobs) as ex:
            all_results = [r for rs in ex.map(lambda f: check(pork, f, flags), files) for r in rs]
    counts = {}
    for f, kind, text, on_path in all_results:
        counts[kind] = counts.get(kind, 0) + 1
        if kind in ("HOLE", "error") or listing:
            path = ", ".join(f"{fn[1]}@{fn[0]} {fn[2]}" for fn in on_path)
            print(f"{kind} {text} [{path}]")
    summary = ", ".join(f"{k} {counts.get(k, 0)}" for k in ("HOLE", "explained", "runtime", "false", "unknown", "error"))
    print(f"analyzer oracle: {len(files)} files, {summary}")
    return 1 if counts.get("HOLE") or counts.get("error") else 0


if __name__ == "__main__":
    sys.exit(main())
