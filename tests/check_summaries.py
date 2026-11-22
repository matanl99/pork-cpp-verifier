#!/usr/bin/env python3
"""Testar lånemodellens std-sammanfattningar (summaries/std.txt) mot körning.

Genererar små program "låna ett element, gör en operation, använd lånet" för
varje behållare x operation x lån. Varje program
  - granskas av pork (--loans=report) på funktionen `probe_<program>`, och
  - byggs med -D_GLIBCXX_DEBUG (ogiltiga iteratorer) och ASan/UBSan (dinglande
    referenser) och körs.

  pork ok,      körningen larmar  -> FEL: osunt (en rad i std.txt eller checkern)
  pork avvisar, körningen larmar  -> bekräftat
  pork avvisar, körningen ren     -> försiktigt (precision, inte fel)
  pork ok,      körningen ren     -> överens

En ren körning bevisar inget (fel kan bero på indata); därför körs växande
operationer i en loop så att omallokering och rehash sker. pork analyserar
koden utan _GLIBCXX_DEBUG (som användare gör); bara körningen har debugläget.

  check_summaries.py <pork> <arbetskatalog> [--jobs N] [--list] [--keep]
                     [--summaries FIL] [--only PREFIX]

Självtest (ctest summaries_runtime_selftest): med en medvetet felaktig regel
(--summaries tests/data/override.summaries) måste skriptet hitta osunda fall.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

CXX = os.environ.get("CXX_FOR_SUMMARIES", "clang++")
# _GLIBCXX_SANITIZE_VECTOR: ASan ser läsning bortom size() (vector::clear frigör inget).
RUN_FLAGS = ["-std=c++23", "-O0", "-g", "-D_GLIBCXX_DEBUG", "-D_GLIBCXX_DEBUG_PEDANTIC", "-D_GLIBCXX_SANITIZE_VECTOR=1",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
LOOP = 64  # > startkapacitet: vector/string/deque/unordered växer och flyttar

# Behållare: typ, include, startvärden (8 element, nycklar 1..8), läsning av
# ett element via iterator och via referens, värde/nyckel ur loopvariabeln.
CONTAINERS = {
    "vector": dict(type="std::vector<int>", inc="vector", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                   it="*it", ref="r", val="i + 100", key=None),
    "array": dict(type="std::array<int, 8>", inc="array", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                   it="*it", ref="r", val="i + 100", key=None),
    "deque": dict(type="std::deque<int>", inc="deque", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                  it="*it", ref="r", val="i + 100", key=None),
    "list": dict(type="std::list<int>", inc="list", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                 it="*it", ref="r", val="i + 100", key=None),
    # Längre än SSO-bufferten (15): annars ligger tecknen i objektet och ASan ser inget.
    "string": dict(type="std::string", inc="string", init='"abcdefghijklmnopqrstuvwxyz012345"',
                   it="*it", ref="r", val="'x'", key=None),
    "map": dict(type="std::map<int, int>", inc="map",
                init="{{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8}}",
                it="it->second", ref="r.second", val="{i + 100, i}", key="i + 100", emplace="i + 100, i"),
    "multimap": dict(type="std::multimap<int, int>", inc="map",
                     init="{{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8}}",
                     it="it->second", ref="r.second", val="{i + 100, i}", key="i + 100", emplace="i + 100, i"),
    "set": dict(type="std::set<int>", inc="set", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                it="*it", ref="r", val="i + 100", key="i + 100", emplace="i + 100"),
    "multiset": dict(type="std::multiset<int>", inc="set", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                     it="*it", ref="r", val="i + 100", key="i + 100", emplace="i + 100"),
    "unordered_map": dict(type="std::unordered_map<int, int>", inc="unordered_map",
                          init="{{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8}}",
                          it="it->second", ref="r.second", val="{i + 100, i}", key="i + 100",
                          emplace="i + 100, i"),
    "unordered_set": dict(type="std::unordered_set<int>", inc="unordered_set", init="{1, 2, 3, 4, 5, 6, 7, 8}",
                          it="*it", ref="r", val="i + 100", key="i + 100", emplace="i + 100"),
}

# Operationer: (namn, kod, loopas). {val} {key} {emplace} ur behållaren; en
# operation som inte kompilerar för en behållare hoppas över.
OPS = [
    ("push_back", "c.push_back({val});", True),
    ("push_front", "c.push_front({val});", True),
    ("emplace_back", "c.emplace_back({val});", True),
    ("emplace_front", "c.emplace_front({val});", True),
    ("insert", "c.insert({val});", True),
    ("insert_end", "c.insert(c.end(), {val});", True),
    ("insert_begin", "c.insert(c.begin(), {val});", True),
    ("emplace", "c.emplace({emplace});", True),
    ("emplace_hint", "c.emplace_hint(c.end(), {emplace});", True),
    ("try_emplace", "c.try_emplace({emplace});", True),
    ("index_new", "c[{key}] = 1;", True),
    ("reserve", "c.reserve(1000);", False),
    ("rehash", "c.rehash(1000);", False),
    ("resize", "c.resize(1000);", False),
    ("shrink", "c.shrink_to_fit();", False),
    ("clear", "c.clear();", False),
    ("pop_back", "c.pop_back();", False),
    ("pop_front", "c.pop_front();", False),
    ("erase_key_other", "c.erase(8);", False),
    ("erase_it_other", "c.erase(std::next(c.begin(), 7));", False),
    ("erase_it_same", "c.erase(c.begin());", False),
    ("erase_range_other", "c.erase(std::next(c.begin(), 6), c.end());", False),
    ("size", "n += static_cast<int>(c.size());", True),
    ("find", "n += c.find(3) == c.end() ? 0 : 1;", True),
    ("count", "n += static_cast<int>(c.count(3));", True),
    ("at", "n += c.at(3);", True),
]

# Lån: deklaration och läsning.
BORROWS = {
    "iterator": ("auto it = c.begin();", "{it}"),
    "reference": ("auto& r = *c.begin();", "{ref}"),
}

PROBE = """\
#include <{inc}>
#include <iterator>

int {fn}({type}& c) {{
  int n = 0;
  {borrow}
{body}
  return n + static_cast<int>({read});
}}

int main() {{
  {type} c = {init};
  volatile int sink = {fn}(c);
  (void)sink;
  return 0;
}}
"""

# `consumes`: radering i en loop med tilldelning tillbaka, och felvarianter.
LOOPS = [
    ("erase_loop", "for (auto it = c.begin(); it != c.end();) {{\n    if ({it} % 2 == 0)\n      it = c.erase(it);\n"
                   "    else\n      ++it;\n  }}\n  return static_cast<int>(c.size());"),
    ("erase_then_use", "auto it = c.begin();\n  c.erase(it);\n  return static_cast<int>({it});"),
    ("erase_into_other", "auto it = c.begin();\n  auto next = c.erase(it);\n"
                         "  return static_cast<int>({it}) + (next == c.end() ? 0 : 1);"),
]

LOOP_PROBE = """\
#include <{inc}>

int {fn}({type}& c) {{
  {body}
}}

int main() {{
  {type} c = {init};
  volatile int sink = {fn}(c);
  (void)sink;
  return 0;
}}
"""


# STN-74: positioner och intervall ur en annan behållare d. Rätt varianter
# (ur c) står bredvid så att oraklet också ser att de godtas och körs rent.
# Bara en körning: _GLIBCXX_DEBUG larmar redan första gången.
POSITIONS = [
    ("insert_pos_same", "c.insert(c.end(), {val});"),
    ("insert_pos_other", "c.insert(d.end(), {val});"),
    ("insert_pos_other_local", "auto it = d.end();\n  c.insert(it, {val});"),
    ("emplace_hint_other", "c.emplace_hint(d.end(), {emplace});"),
    ("erase_pos_same", "c.erase(c.begin());"),
    ("erase_pos_other", "c.erase(d.begin());"),
    ("erase_pos_other_local", "auto it = d.begin();\n  c.erase(it);"),
    ("erase_range_same", "c.erase(c.begin(), c.end());"),
    ("erase_range_mixed", "auto b = c.begin();\n  auto e = d.end();\n  c.erase(b, e);"),
    # STN-77/78: intervall till fria algoritmer.
    ("sort_same", "std::sort(c.begin(), c.end());"),
    ("sort_mixed", "std::sort(c.begin(), d.end());"),
    ("sort_mixed_local", "auto b = c.begin();\n  auto e = d.end();\n  std::sort(b, e);"),
    ("find_mixed", "auto b = c.begin();\n  auto e = d.end();\n  i = std::find(b, e, *b) == b ? 1 : 0;"),
    ("copy_other", "std::copy(d.begin(), d.end(), c.begin());"),
    ("fill_same", "std::fill(c.begin(), c.end(), *c.begin());"),
    # i == 0: villkoret väljer d, alltså ett intervall från d till c.
    ("sort_conditional", "auto first = i == 1 ? c.begin() : d.begin();\n  std::sort(first, c.end());"),
    ("compare_conditional", "auto it = i == 1 ? c.begin() : d.begin();\n  i = it == c.end() ? 1 : 0;"),
    ("move_mixed", "auto out = c;\n  std::move(c.begin(), d.end(), out.begin());"),
]

POSITION_PROBE = """\
#include <{inc}>
#include <algorithm>

int {fn}({type}& c, {type}& d) {{
  int i = 0;
  {body}
  return static_cast<int>(c.size() + d.size()) + i;
}}

int main() {{
  {type} c = {init};
  {type} d = {init};
  volatile int sink = {fn}(c, d);
  (void)sink;
  return 0;
}}
"""


def probe_name(name: str) -> str:
    # Unikt per program: pork slår ihop funktioner med samma USR över TU:er.
    return "probe_" + re.sub(r"\W", "_", name)


def programs():
    for cname, c in CONTAINERS.items():
        fields = {k: v for k, v in c.items() if isinstance(v, str)}
        for opname, code, loop in OPS:
            if ("{key}" in code or "{emplace}" in code) and c.get("key") is None and "{emplace}" not in code:
                continue
            if "{emplace}" in code:
                stmt = code.format(emplace=c.get("emplace", c["val"]))
            elif "{key}" in code:
                stmt = code.format(key=c["key"])
            else:
                stmt = code.format(val=c["val"])
            body = (f"  for (int i = 0; i < {LOOP}; ++i) {stmt}" if loop else f"  {stmt}")
            for bname, (decl, read) in BORROWS.items():
                src = PROBE.format(fn=probe_name(f"{cname}.{opname}.{bname}"), inc=c["inc"], type=c["type"], init=c["init"], borrow=decl, body=body,
                                   read=read.format(it=c["it"], ref=c["ref"]))
                yield f"{cname}.{opname}.{bname}", src
        for lname, body in LOOPS:
            src = LOOP_PROBE.format(fn=probe_name(f"{cname}.{lname}"), inc=c["inc"], type=c["type"], init=c["init"], body=body.format(it=c["it"]))
            yield f"{cname}.{lname}", src
        for pname, code in POSITIONS:
            body = code.format(val=c["val"], emplace=c.get("emplace", c["val"]))
            src = POSITION_PROBE.format(fn=probe_name(f"{cname}.{pname}"), inc=c["inc"], type=c["type"], init=c["init"], body=body)
            yield f"{cname}.{pname}", src


def build_and_run(path: Path):
    exe = path.with_suffix("")
    r = subprocess.run([CXX, *RUN_FLAGS, str(path), "-o", str(exe)], capture_output=True, text=True)
    if r.returncode != 0:
        return "skip", r.stderr
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:exitcode=99",
               UBSAN_OPTIONS="print_stacktrace=0:halt_on_error=1:exitcode=99")
    r = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=60)
    if r.returncode == 0:  # main returnerar alltid 0; ASan/UBSan/debugläget avslutar annorlunda
        return "clean", ""
    first = next((l for l in r.stderr.splitlines()
                  if "Error:" in l or "ERROR:" in l or "runtime error" in l or "attempt to" in l), "")
    return "alarm", first.strip() or f"exit {r.returncode}"


def pork_verdicts(pork: str, work: Path, files, extra):
    db = [{"directory": str(work), "file": str(f), "command": f"clang++ -std=c++23 -c {f}"} for f in files]
    (work / "compile_commands.json").write_text(json.dumps(db))
    r = subprocess.run([pork, "-p", str(work), "--loans=report", *extra, *map(str, files)],
                       capture_output=True, text=True)
    crash = next((l for l in r.stderr.splitlines() if "ERROR: AddressSanitizer" in l or "runtime error" in l), None)
    if r.returncode not in (0, 1) or crash:
        sys.exit(f"pork failed ({r.returncode}): {crash or r.stderr.strip()[:500]}")
    verdicts = {}
    current = None
    for line in r.stdout.splitlines():
        m = re.match(r"^(\S+?):\d+:\d+: probe_\w+: ", line)
        if m:
            current = Path(m.group(1)).name
            continue
        if current and line.startswith("  loans: "):
            verdicts[current] = line[len("  loans: "):].split("; local:")[0]
            current = None
    return verdicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pork")
    ap.add_argument("work")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--list", action="store_true", help="list cautious rejections too")
    ap.add_argument("--keep", action="store_true", help="keep the work directory")
    ap.add_argument("--summaries", help="extra summaries file passed to pork")
    ap.add_argument("--only", default="", help="only programs whose name starts with this")
    args = ap.parse_args()

    if shutil.which(CXX) is None:
        print(f"SKIP: {CXX} not found")
        return 0
    work = Path(args.work).resolve()
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)

    files = {}
    for name, src in programs():
        if not name.startswith(args.only):
            continue
        p = work / f"{name}.cpp"
        p.write_text(src)
        files[p.name] = p

    with ThreadPoolExecutor(args.jobs) as pool:
        runs = dict(zip(files, pool.map(build_and_run, files.values())))
    built = [files[n] for n, (status, _) in runs.items() if status != "skip"]
    extra = [f"--summaries={Path(args.summaries).resolve()}"] if args.summaries else []
    verdicts = pork_verdicts(args.pork, work, built, extra)

    unsound, confirmed, cautious, agree, missing = [], [], [], [], []
    for name in sorted(n for n in runs if runs[n][0] != "skip"):
        status, why = runs[name]
        v = verdicts.get(name)
        if v is None:
            missing.append(name)
        elif v == "ok" and status == "alarm":
            unsound.append((name, why))
        elif v == "ok":
            agree.append(name)
        elif status == "alarm":
            confirmed.append((name, v))
        else:
            cautious.append((name, v))

    for name, why in unsound:
        print(f"UNSOUND {name}: pork accepts, run: {why}")
    for name in missing:
        print(f"MISSING {name}: no pork verdict for probe")
    if args.list:
        for name, v in cautious:
            print(f"  cautious {name}: {v}")
    skipped = sum(1 for s, _ in runs.values() if s == "skip")
    print(f"programs {len(runs) - skipped} (skipped {skipped} that do not compile): "
          f"agree {len(agree)}, confirmed rejections {len(confirmed)}, "
          f"cautious rejections {len(cautious)}, unsound {len(unsound)}")
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if unsound or missing else 0


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.exit(main())
