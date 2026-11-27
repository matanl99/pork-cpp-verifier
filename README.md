# Pork

**Version 0.1.0 · Technical Preview**

Pork is an experimental tool for formal verification of C++. It uses Clang
to identify functions in an analyzable subset of C++, check declared data
dependencies, and prove the absence of selected runtime errors. The project
draws on the
[SPARK assurance levels](https://docs.adacore.com/spark2014-docs/html/ug/en/usage_scenarios.html).
Pork is not affiliated with AdaCore or the SPARK project.

| Level | What Pork checks |
|---|---|
| **Pork Stone** | The function and its callees meet rules for the analyzable C++ subset, including initialization, side effects, aliasing, and lifetimes. [Specification](spec/1_stone.txt) |
| **Pork Bronze** | A Stone function satisfies its declared data-flow requirements. Use `PORK_DEPENDS` to specify which inputs an output may depend on. [Specification](spec/3_bronze.txt) |
| **Pork Silver** | Pork translates the function to C and checks for runtime errors such as division by zero, signed overflow, and out-of-bounds access. The proof depends on the translation, the function's preconditions, and any reported assumptions. A failed Bronze requirement or an execution that can exceed the unwinding limit prevents a Silver verdict. [Specification](spec/2_silver.txt) |

What each verdict promises, and how that promise is tested, is described in
[Verification confidence](docs/verification-confidence.md).

## Build

You need CMake 3.28 or later, Ninja for the commands below, a C++23 compiler,
and LLVM/Clang 23 with development libraries and CMake package files. Put a
matching `clang` on `PATH`. CMake also requires GoogleTest and Python 3
because the build configures the test suite.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/pork --version
```

### Silver provers and solvers

Stone and Bronze run without a prover. For Silver, put the prover executable
on `PATH`.

| Prover | Default solver | Optional solver selection |
|---|---|---|
| [CBMC](https://github.com/diffblue/cbmc) 6.x (default) | CaDiCaL when `cadical` is on `PATH`, otherwise CBMC's built-in MiniSAT, which requires no separate installation. | Use `--silver-solver minisat`, `z3`, or `cvc5` with the corresponding executable on `PATH`. |
| [ESBMC](https://github.com/esbmc/esbmc) (`--silver-prover=esbmc`) | Use an ESBMC build with Bitwuzla support. | Use `--silver-solver z3`, for example, with an ESBMC build that includes Z3 support. |

### Silver runtime

Silver can take several minutes on a larger project, especially with an
empty cache or when the prover has to unwind many loop iterations.
`--silver-timeout` defaults to 120 seconds per proof attempt. Set a shorter
limit such as `--silver-timeout=10` for a quick first pass. With the cache
enabled, a later run with a higher limit can reuse completed results and
retry timed-out proofs. A timeout leaves the proof inconclusive.
[Measured runtimes](docs/reference.sv.md#silver---silver)

## Quick start

Save this as `example.cpp` in the Pork repository:

```cpp
#include <pork/annotations.hpp>

int add(int a, int b) {
  return a + b;
}

int ratio(int x, int log_level) {
  PORK_PRE(x > 0);
  PORK_DEPENDS_RESULT(x);
  return 100 / x;
}
```

```sh
./build/pork example.cpp -- -std=c++20 -Iinclude
./build/pork --silver example.cpp -- -std=c++20 -Iinclude
```

The first command reports `add: stone` and `ratio: bronze`. Both functions
are Stone, and the result of `ratio` depends only on `x`, as
`PORK_DEPENDS_RESULT` requires. The second command reports `ratio: silver`:
the precondition rules out division by zero. For `add` it reports
`add: stone: not silver: arithmetic overflow on signed + in a + b`, because
nothing limits the inputs. The exit status is still 0: a verdict only fails
the run when the function carries a requirement such as `PORK_STONE` or
`PORK_DEPENDS`.

Change the return statement in `ratio` to `return 100 / x + log_level;` and run the
first command again. Pork reports `ratio: stone: not bronze: the result
depends on log_level, which PORK_DEPENDS does not allow` and exits with
status 1.

Flags after `--` apply to the source being analyzed. Use the C++ standard
your source actually builds with. In particular, `PORK_DEPENDS` uses
`__VA_OPT__`, which was standardized in C++20.

## Analyze an existing project

For a CMake project using Ninja, generate a compilation database and run
Pork from the target project's root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
/path/to/pork/build/pork -p build src/main.cpp src/core/*.cpp
```

`-p build` points to the directory containing `compile_commands.json`.
The file arguments select the translation units to analyze. Include all
relevant source files to cover the whole project. Add `--flow` to show
inferred dependencies or `--silver` to check for runtime errors. Pork also
warns about statements and conditions in Stone functions that have no effect,
such as `if (valid);` or a check whose result is discarded (`--no-warnings`
turns this off). Run
`./build/pork --help` from the Pork repository for more options.

## Use as a check for AI-generated code

Pork can serve as a rule-based check for code written by an AI coding
assistant. Its verdicts are machine-readable, one line per function with a
reason, and `PORK_STONE` and `PORK_DEPENDS` turn them into requirements: Pork
exits with status 1 when a marked function is not Stone or breaks its
declared data flow. An assistant can run Pork after each change and revise
the code until the requirements hold. Because Stone is an allowlist,
constructs Pork does not know are rejected rather than accepted.

A passing check means the function meets Pork's rules, not that its logic
is correct. Stone covers initialization, side effects, aliasing, and
lifetimes; Bronze covers declared data flow; Silver covers selected runtime
errors. An assistant can also make a check pass by weakening it, for
example by adding entries to the trust file, adding `PORK_PRE`
preconditions, or removing `PORK_STONE` and `PORK_DEPENDS`. Pork reports
assumptions in every verdict, but the trust file and the annotations should
be reviewed by a person and protected in CI.

## Why Silver translates C++ to C

Clang resolves C++ types, overloads, template instantiations, and implicit
conversions. [`CTranslate.cpp`](adapters/clang/CTranslate.cpp) uses that
typed AST to express supported operations as C. ESBMC can also analyze C++
directly through its own Clang frontend. Pork Silver uses the generated C
with either prover so that CBMC and ESBMC check the same model, including
Pork's contracts and library bounds. Some constructs in the Stone subset
still lack a Silver translation.
In those cases Pork reports why the translation could not be made.

For example, a writable `T&` parameter becomes a C pointer, and a supported
`std::optional<T>` becomes a struct with a presence flag and a value field.
A proof harness supplies arbitrary inputs subject to the function's
preconditions and the model's bounds. At analyzed calls those preconditions
are checked. Source locations in the generated C let Pork report findings
against the original C++.

A Silver verdict relies on the translation preserving the behavior relevant
to the checks. Use `--keep-c` to inspect the prover's input.

The [equivalence prototype](tests/check_equivalence.py) runs separately
from Silver. It builds a `.eq.cpp` harness containing the original C++
function and its generated C translation, then runs ESBMC with
`--std c++20`. It compares return values and changes to reference arguments
to look for differences between the two versions.

[Silver guide](docs/silver.md) · [Equivalence notes](docs/equivalence.md)

## Generated files

| When | Files | How they are used |
|---|---|---|
| Configure and build Pork | `build/pork`, `build/pork_unit_tests`, and `build/compile_commands.json` | Run `pork` to analyze code and use CTest to run the test suite. The compilation database records the compiler flags for Pork's own sources. To analyze another project, use that project's database. |
| Configure Pork | `build/generated/std.txt` and `build/generated/StdSummaries.cpp` | CMake copies the standard-library loan summaries and embeds their contents in the executable through the generated C++ source. Edit `summaries/std.txt` to change the build input. |
| Run Silver | Temporary C programs and prover output | Pork passes the generated C to CBMC or ESBMC and reads the prover's result before cleaning up the temporary files. ESBMC also receives a temporary compatibility header. |
| Run Silver with caching enabled | JSON files under `$XDG_CACHE_HOME/pork/silver`, or `~/.cache/pork/silver` by default | Pork reuses stored prover results for matching inputs and settings. Use `--no-silver-cache` to run without reading or writing the cache. |

Pork prints analysis verdicts to standard output and diagnostics to standard
error. To inspect the exact C input sent to the prover, run the quick-start
example with `--keep-c`:

```sh
./build/pork --silver --keep-c build/proof-inputs example.cpp -- -std=c++20 -Iinclude
```

This keeps numbered files such as `build/proof-inputs/silver_1.c`. Use them
to inspect the translation or investigate a Silver finding. With
`--keep-c`, Pork bypasses the cache so the C files are actually generated.
Use a fresh output directory to retain files from multiple runs.

## Documentation and tests

- [Analysis flow diagram](docs/analysis-flow.svg) ([Graphviz DOT source](docs/analysis-flow.dot)).
- [Verification confidence matrices](docs/verification-confidence.md), covering
  what each level promises, how it is tested, and where the evidence ends.
- [What each level covers](docs/coverage.md), by kind of error, including
  what no level covers.
- [How Pork checks rules from the Clang AST](docs/verification-confidence.md#how-pork-checks-rules-from-the-clang-ast),
  with examples for Stone, Bronze, and Silver.
- [Stone's loan model](docs/stone_model.md), [Bronze data flow](docs/bronze.md),
  and [the Silver implementation](docs/silver.md) (in Swedish).
- [Svensk dokumentation och mätresultat](docs/reference.sv.md).
- [Test corpus](corpus/README.md), with examples tied to specification rules.

Run the test suite with `ctest --test-dir build --output-on-failure` from
the Pork repository.

## License

[Mozilla Public License 2.0](LICENSE). Copyright 2026 Mathias Anlér.

## Author

Mathias Anlér

Developed with assistance from Codex and Claude.
