#!/usr/bin/env python3
"""Prototyp: bevisa att pork:s C-översättning beter sig som C++-originalet.

För varje C-program som `pork --silver --keep-c` skriver ut byggs en C++-
testram som ESBMC (med sin C++-frontend) verifierar:

  - C-översättningen kompileras som C++ (`extern "C"`); pork:s egna
    kontroller (`__CPROVER_assert`) blir antaganden, eftersom Silver bevisar
    dem separat. Likheten gäller alltså där C-versionen är definierad.
  - Originalfilen inkluderas som den är.
  - pork:s testram (`pork_harness`) återanvänds: samma godtyckliga indata och
    antaganden (storlek <= --vector-bound, ändliga flyttal, förvillkor).
  - Varje argument omvandlas från C-strukturen till parameterns C++-typ
    (vector, string, optional, struct med samma fältnamn, skalärer); båda
    versionerna anropas; returvärdet och varje argument som skickas som pekare
    (T&) jämförs efteråt.

Resultat per funktion:
  equal       bevisat lika inom gränserna
  DIFFERENT   motexempel: översättningen skiljer sig (eller ESBMC:s std-modell)
  bounded     en loop rullades inte ut helt (öka --unwind)
  trusted     C-versionen anropar betrodda funktioner, eller egna genom
              sitt kontrakt (PORK_POST: havoc + antaget eftervillkor): en
              abstraktion, ingen jämförelse
  esbmc       ESBMC larmar i originalet eller sin std-modell, inte i
              jämförelsen (t.ex. kopiering av std::optional<struct med
              struct-fält> gav "object out of bounds" med ESBMC 8.6)
  skipped     testramen kompilerar inte (metoder, överlagringar, typer som
              ännu inte har en omvandling) eller ESBMC gav inget svar

Användning:
  check_equivalence.py <pork> <fil.cpp>... [--vector-bound N] [--unwind N]
                       [--keep DIR] [--only NAMN]
ESBMC körs med minnestak (systemd-run) och tidsgräns.
"""

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

COMPAT = r"""// pork: C-översättningen kompilerad som C++ (check_equivalence.py).
#define _Bool bool
#define __CPROVER_assert(c, msg) __ESBMC_assume(c)
#define __CPROVER_assume(c) __ESBMC_assume(c)
#define __CPROVER_isnanf(x) __builtin_isnan(x)
#define __CPROVER_isinff(x) __builtin_isinf(x)
#define __CPROVER_isnand(x) __builtin_isnan(x)
#define __CPROVER_isinfd(x) __builtin_isinf(x)
"""

# Omvandling C-struktur -> C++-typ och jämförelse, styrd av C++-typen.
CONVERT = r"""
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace pork_eq {
template <class T> struct is_vector : std::false_type {};
template <class T, class A> struct is_vector<std::vector<T, A>> : std::true_type {};
template <class T> struct is_optional : std::false_type {};
template <class T> struct is_optional<std::optional<T>> : std::true_type {};
template <class T> constexpr bool is_string = std::is_same_v<T, std::string>;

// Parametrarna: arg<0> är objektet för en metod (C: första argumentet).
template <class F> struct params;
template <class R, class... A> struct params<R (*)(A...)> {
  template <unsigned I> using arg = std::remove_cvref_t<std::tuple_element_t<I, std::tuple<A...>>>;
};
template <class R, class C, class... A> struct params<R (C::*)(A...)> {
  template <unsigned I> using arg = std::remove_cvref_t<std::tuple_element_t<I, std::tuple<C, A...>>>;
};
template <class R, class C, class... A> struct params<R (C::*)(A...) const> {
  template <unsigned I> using arg = std::remove_cvref_t<std::tuple_element_t<I, std::tuple<C, A...>>>;
};

template <class F, class O, class... A> decltype(auto) call(F f, O& o, A&... a) {
  if constexpr (std::is_member_function_pointer_v<F>) return (o.*f)(a...);
  else return f(o, a...);
}
template <class F> decltype(auto) call(F f) { return f(); }

// Fyll på plats, och alltid en lokal som sedan tilldelas: med ESBMC 8.6
// observerades att värdet gick förlorat när std::optional<struct> returnerades
// som värde, och att en skrivning genom en referens in i optional:s lagring
// lästes med fel typ (float som int).
template <class Cpp, class C> void fill(Cpp& out, const C& c);
template <class C, class Cpp> bool eq(const C& c, const Cpp& x);

template <class T> bool same(const T& a, const T& b) {
  if constexpr (std::is_floating_point_v<T>) return a == b || (a != a && b != b);
  else return a == b;
}

template <class Cpp, class C> void fill(Cpp& out, const C& c) {
  if constexpr (std::is_arithmetic_v<Cpp> || std::is_enum_v<Cpp>) {
    out = static_cast<Cpp>(c);
  } else if constexpr (is_vector<Cpp>::value || is_string<Cpp>) {
    out.clear();
    for (unsigned long i = 0; i < c.size; ++i) {
      typename Cpp::value_type e{};
      fill(e, c.data[i]);
      out.push_back(e);
    }
  } else if constexpr (is_optional<Cpp>::value) {
    if (!c.has) {
      out.reset();
    } else {
      typename Cpp::value_type v{};
      fill(v, c.v);
      out = v;
    }
  } else {
    fill_struct(out, c);  // genererad per C-struktur
  }
}

template <class C, class Cpp> bool eq(const C& c, const Cpp& x) {
  if constexpr (std::is_arithmetic_v<Cpp> || std::is_enum_v<Cpp>) {
    return same(static_cast<Cpp>(c), x);
  } else if constexpr (is_vector<Cpp>::value || is_string<Cpp>) {
    if (c.size != x.size()) return false;
    for (unsigned long i = 0; i < c.size; ++i)
      if (!eq(c.data[i], x[i])) return false;
    return true;
  } else if constexpr (is_optional<Cpp>::value) {
    if (bool(c.has) != x.has_value()) return false;
    return !c.has || eq(c.v, *x);
  } else {
    return eq_struct(c, x);  // genererad per C-struktur
  }
}
}  // namespace pork_eq
"""

STRUCT = re.compile(r"^struct (\w+) \{\n(.*?)^\};", re.S | re.M)
FIELD = re.compile(r"^\s+(?:const\s+)?[\w\s]+?[\s*](\w+)(?:\[\d+\])?;\s*$")
ALTERNATIVE = re.compile(r"^\s+struct \w+ (a\d+);\s*$|^\s+[\w\s]+? (a\d+);\s*$", re.M)
TARGET = re.compile(r"^\s*(?:(\w[\w\s*]*?)\s+)?(?:\w+\s*=\s*)?(f_\w+?)__entry\((.*)\);\s*$", re.M)
HARNESS = re.compile(r"^void pork_harness\(void\)\n\{\n(.*?)^\}", re.S | re.M)
DECLARATION = re.compile(r"^[A-Za-z_][\w\s*]*?\b(\w+)\([^;{}]*\);$", re.M)


def structs_of(c_text):
    """Egna typer (s_<namn>_<hash>: fältnamn) och varianter (x_...: alternativ a0..an)."""
    users, variants = [], []
    for name, body in STRUCT.findall(c_text):
        if name.startswith("s_"):
            fields = [m.group(1) for m in map(FIELD.match, body.splitlines()) if m]
            users.append((name, fields))
        elif name.startswith("x_"):
            alts = [a or b for a, b in ALTERNATIVE.findall(body)]
            variants.append((name, alts))
    return users, variants


def trusted_calls(c_text):
    """Funktioner som bara deklareras (betrodda, kontrakt): ESBMC ger dem ett
    godtyckligt resultat, så C-versionen är en abstraktion, inte en översättning."""
    out = []
    for m in DECLARATION.finditer(c_text):
        name = m.group(1)
        if not re.search(r"^[^\n#;]*\b" + name + r"\([^;{}]*\)\n\{", c_text, re.M):
            out.append(name)
    return out


def cpp_name(c_function):
    """f_<namn>_<hash8> -> namn (`__` = `::`, även Klass::metod)."""
    m = re.fullmatch(r"f_(\w+)_[0-9a-f]{8}", c_function)
    return m.group(1).replace("__", "::") if m else None


def make_harness(c_file, source, c_text):
    h = HARNESS.search(c_text)
    if not h:
        return None, None, "no pork_harness"
    body = h.group(1)
    t = TARGET.search(body)
    if not t:
        return None, None, "no call of the function in pork_harness"
    c_function, args_text = t.group(2), t.group(3)
    name = cpp_name(c_function)
    if name is None:
        return None, None, f"cannot map {c_function} to C++"
    trusted = trusted_calls(c_text)
    if trusted:
        return None, name, "calls trusted functions (contracts): " + ", ".join(trusted)
    if re.search(r"\bpork_h\d+\b", c_text.split("void pork_harness(void)")[0]):
        # En anropad funktion med PORK_POST (eller dispatch) är ersatt av sitt
        # kontrakt: havoc + antaget eftervillkor. Också en abstraktion.
        return None, name, "calls a function through its contract (havoc + post)"
    proto = re.search(r"^(.*?)\b" + c_function + r"\(", c_text, re.M)
    returns_void = proto is not None and proto.group(1).strip() == "void"
    args = [a.strip() for a in args_text.split(",")] if args_text.strip() else []
    setup = body[: t.start()]  # deklarationer och antaganden

    users, variants = structs_of(c_text)
    gen, decls = [], []
    for sname, fields in users:
        fills = "".join(f"  fill(out.{f}, c.{f});\n" for f in fields)
        compares = " && ".join(f"eq(c.{f}, x.{f})" for f in fields) or "true"
        decls.append(f"template <class Cpp> void fill_struct(Cpp& out, const {sname}& c);\n"
                     f"template <class Cpp> bool eq_struct(const {sname}& c, const Cpp& x);\n")
        gen.append(f"template <class Cpp> void fill_struct(Cpp& out, const {sname}& c) {{\n{fills}}}\n")
        gen.append(f"template <class Cpp> bool eq_struct(const {sname}& c, const Cpp& x) {{ return {compares}; }}\n")
    for vname, alts in variants:
        cases = "".join(f"  if (c.index == {i}ul) {{ std::remove_cvref_t<decltype(std::get<{i}>(out))> v{{}}; fill(v, c.u.{a}); out = v; }}\n"
                        for i, a in enumerate(alts))
        compares = " || ".join(f"(c.index == {i}ul && eq(c.u.{a}, std::get<{i}>(x)))" for i, a in enumerate(alts))
        decls.append(f"template <class Cpp> void fill_struct(Cpp& out, const {vname}& c);\n"
                     f"template <class Cpp> bool eq_struct(const {vname}& c, const Cpp& x);\n")
        gen.append(f"template <class Cpp> void fill_struct(Cpp& out, const {vname}& c) {{\n"
                   f"  __ESBMC_assume(c.index < {len(alts)}ul);\n{cases}}}\n")
        gen.append(f"template <class Cpp> bool eq_struct(const {vname}& c, const Cpp& x) {{\n"
                   f"  return c.index == x.index() && ({compares or 'false'});\n}}\n")

    lines = [COMPAT, 'extern "C" {', f'#include "{c_file}"', "}", f'#include "{source}"']
    convert = CONVERT.replace("namespace pork_eq {\n", "namespace pork_eq {\n" + "".join(decls), 1)
    convert = convert.replace("}  // namespace pork_eq\n", "".join(gen) + "}  // namespace pork_eq\n")
    lines.append(convert)
    lines.append("int main() {")
    lines.append("  using namespace pork_eq;")
    lines.append(f"  using P = params<decltype(&{name})>;")
    lines.append(setup)
    cpp_args = []
    for i, a in enumerate(args):
        var = a.lstrip("&").strip()
        lines.append(f"  P::arg<{i}> pork_cpp{i}{{}};")
        lines.append(f"  fill(pork_cpp{i}, {var});")
        cpp_args.append(f"pork_cpp{i}")
    c_call = f"{c_function}__entry({', '.join(args)})"
    cpp_call = f"call({', '.join([f'&{name}'] + cpp_args)})"
    if returns_void:
        lines.append(f"  {c_call};")
        lines.append(f"  {cpp_call};")
    else:
        lines.append(f"  auto pork_rc = {c_call};")
        lines.append(f"  auto pork_rv = {cpp_call};")
        lines.append(f'  __ESBMC_assert(eq(pork_rc, pork_rv), "pork-eq: result");')
    for i, a in enumerate(args):
        if a.startswith("&"):
            var = a.lstrip("&").strip()
            lines.append(f'  __ESBMC_assert(eq({var}, pork_cpp{i}), "pork-eq: argument {i} after the call");')
    lines.append("}")
    return "\n".join(lines) + "\n", name, None


def run_esbmc(harness, unwind, timeout):
    cmd = ["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=4G", "-p", "MemorySwapMax=0",
           "timeout", str(timeout), "esbmc", str(harness), "--std", "c++20", "-I", str(ROOT / "include"),
           "--unwind", str(unwind)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = p.stdout + p.stderr
    if "VERIFICATION SUCCESSFUL" in out:
        return "equal", ""
    if "VERIFICATION FAILED" in out:
        m = re.search(r"Violated property:\n(.*?)\n\n", out, re.S)
        violated = m.group(1).strip() if m else "?"
        if "pork-eq" in violated:
            values = re.findall(r"^  (pork_\w+(?:\.\w+)*(?:\[\d+\])?(?:\.\w+)* = .*?)(?: \(|$)", out, re.M)
            return "DIFFERENT", violated.splitlines()[-1] + "\n" + "\n".join(values[-12:])
        if "unwinding assertion" in violated:
            return "bounded", ""
        return "esbmc", " ".join(violated.split())[:200]
    m = re.search(r"error: (.*)", out)
    if m:
        return "skipped", m.group(1).strip()[:200]
    return "skipped", "no verdict (timeout or crash)"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pork")
    ap.add_argument("sources", nargs="+")
    ap.add_argument("--vector-bound", type=int, default=3)
    ap.add_argument("--unwind", type=int, default=6)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--keep", help="spara C-program och testramar här")
    ap.add_argument("--only", help="bara funktioner med detta namn")
    opts = ap.parse_args()
    if shutil.which("esbmc") is None:
        print("esbmc not found")
        return 77

    work = Path(opts.keep) if opts.keep else Path(tempfile.mkdtemp(prefix="pork-eq-"))
    work.mkdir(parents=True, exist_ok=True)
    counts = {}
    for src in map(Path, opts.sources):
        src = src.resolve()
        cdir = work / src.stem / "c"
        shutil.rmtree(cdir, ignore_errors=True)
        cdir.mkdir(parents=True)
        cmd = [opts.pork, "--silver", "--no-silver-cache", "--vector-bound", str(opts.vector_bound),
               "--keep-c", str(cdir)]
        for extra in (src.with_suffix(".trust"),):
            if extra.exists():
                cmd += ["--trust", str(extra)]
        if src.with_suffix(".contracts.c").exists():
            cmd += ["--contracts", str(src.with_suffix(".contracts.c"))]
        cmd += [str(src), "--", "-std=c++23", f"-I{ROOT / 'include'}"]
        subprocess.run(["systemd-run", "--user", "--scope", "-q", "-p", "MemoryMax=8G", "-p", "MemorySwapMax=0",
                        "timeout", "900", *cmd], capture_output=True, text=True)
        for c_file in sorted(cdir.glob("*.c"), key=lambda p: (len(p.name), p.name)):
            c_text = c_file.read_text()
            harness, name, why = make_harness(c_file, src, c_text)
            if name is None:
                continue  # t.ex. självtest av förvillkor (ingen funktion att jämföra)
            if opts.only and name != opts.only:
                continue
            if harness is None:
                counts["trusted"] = counts.get("trusted", 0) + 1
                print(f"{'trusted':9} {src.name}: {name}  ({why})")
                continue
            hfile = c_file.with_suffix(".eq.cpp")
            hfile.write_text(harness)
            verdict, detail = run_esbmc(hfile, opts.unwind, opts.timeout)
            counts[verdict] = counts.get(verdict, 0) + 1
            print(f"{verdict:9} {src.name}: {name}" + (f"  ({detail})" if detail and verdict != "DIFFERENT" else ""))
            if verdict == "DIFFERENT":
                print("    " + detail.replace("\n", "\n    "))
            sys.stdout.flush()
    print("summary: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    if opts.keep is None:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if counts.get("DIFFERENT") else 0


if __name__ == "__main__":
    sys.exit(main())
