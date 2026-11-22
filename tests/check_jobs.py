"""--jobs ändrar inte domarna: hela korpusen i en körning med en tråd och med
flera ska ge byte-identisk utdata och samma exit-kod (STN-46/48: samma kod,
samma dom). Användning: check_jobs.py <pork> <corpus>"""
import pathlib, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

pork, corpus = sys.argv[1], pathlib.Path(sys.argv[2])
files = sorted(str(p) for p in corpus.rglob("*.cpp"))
def run(jobs):
    args = ["--", "-std=c++23", f"-I{corpus.parent / 'include'}"]
    r = subprocess.run([pork, f"--jobs={jobs}", *files, *args], capture_output=True, text=True)
    return r.returncode, r.stdout
# De tre körningarna samtidigt: testet jämför utdata, inte tider.
with ThreadPoolExecutor(3) as pool:
    one, *others = pool.map(run, (1, 2, 8))
if one[1].count("\n") < 500:
    sys.exit(f"too few verdicts with --jobs=1 ({one[1].count(chr(10))}), the test checks nothing")
for jobs, other in zip((2, 8), others):
    if other != one:
        sys.exit(f"--jobs={jobs} differs from --jobs=1 (exit {other[0]} vs {one[0]})")
print(f"{len(files)} files, {one[1].count(chr(10))} verdicts, identical with --jobs=1/2/8")
