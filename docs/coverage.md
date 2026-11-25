# What each Pork level covers

The specifications ([Stone](../spec/1_stone.txt), [Bronze](../spec/3_bronze.txt),
[Silver](../spec/2_silver.txt)) are ordered by rule. This page is ordered by
kind of error: for each area it says which level rules it out, under which
conditions, and what no level covers. How each guarantee is tested is in
[Verification confidence](verification-confidence.md).

Levels are cumulative. A Silver function is also Bronze and Stone, and a
Stone verdict means the function and everything it calls is Stone or
trusted (STN-23).

Legend:

- **Ruled out:** the level rejects every function where the error can occur.
- **Checked:** the level proves the error unreachable for admissible inputs,
  or the function does not get the verdict.
- **Assumed:** the level relies on it without checking it. The verdict
  reports the assumption, or it follows from the conditions below.
- **—:** the level says nothing about it.

## Memory and lifetime

| Area | Stone | Bronze | Silver | Not covered, assumptions | Rules |
|---|---|---|---|---|---|
| Dangling references, use after free, invalidated iterators | Ruled out (loan model) | — | Relies on Stone | Library operations follow `summaries/std.txt`. Owned-array and vector indices are assumed in bounds by the loan model (see out-of-bounds access). | STN-31–36, STN-39, STN-62, STN-74 |
| Aliasing between arguments, `*this` and hidden references | Ruled out | — | Relies on Stone | Calls through ports and context references count as side effects. Calls from outside the analysis are not checked: such a caller can pass the same object twice. | STN-20, STN-21, STN-26, STN-53–55, STN-62 |
| Raw pointers and ownership | Ruled out, except loan values (`get_if`, iterators, span), `unique_ptr` without raw-pointer transfer | — | — | `std::unique_ptr` is a trusted type. `p.get()` is a loan of the owned object (a use after `p.reset()` or a move of `p` is rejected); `p.release()` and construction or `reset` from a raw pointer are rejected, except `std::unique_ptr<T> t{o.release()}` and `t.reset(o.release())` with the same type. A `unique_ptr` may be moved out of a field or an element and checked afterwards (STN-90). `std::shared_ptr` and `std::weak_ptr` are rejected, also when trusted (shared ownership). | STN-3, STN-12, STN-19, STN-39, STN-69, STN-88, STN-90 |
| Null dereference | `std::unique_ptr` (`*p`, `p->`) and local pointers from `get()` or `nullptr` | — | Checked (`get_if`, nullable return) | Stone: an empty `std::unique_ptr` is not dereferenced (after `reset()`, `= nullptr`, a move, default construction or `{nullptr}`; a check `if (p)` or an assignment from `make_unique` makes it non-empty). Parameters and `*this` are assumed non-empty and the caller is checked; a call without a summary in the translation unit may empty what it can change. Raw pointer parameters are not checked by Stone. Pointer forms Silver does not translate give `not silver`, and Silver does not translate `unique_ptr`. | STN-89, SLV-12, SLV-27 |
| Out-of-bounds access: `std::vector`, `std::span`, `std::string` | Not checked | — | Checked | Proved only up to `--vector-bound` elements (reported assumption). Growing past the bound is a finding. | SLV-13, SLV-15, SLV-22, SLV-24, SLV-35 |
| Out-of-bounds access: owned C arrays and `std::array` | Not checked | Index assumed in bounds | Not translated (`not silver`) | No level checks the index yet. | STN-75 |
| `std::optional` without a value | Not checked | — | Checked | | SLV-14 |
| Reads of uninitialized values | Ruled out | — | Relies on Stone | Scalars without initializer use clang's uninitialized-values analysis. | STN-15–17, STN-2, STN-66 |

## Arithmetic

| Area | Stone | Bronze | Silver | Not covered, assumptions | Rules |
|---|---|---|---|---|---|
| Signed integer overflow | — | — | Checked | | SLV-5 |
| Division and remainder by zero (integer and floating point) | — | — | Checked | | SLV-6, SLV-7 |
| Shifts out of range | — | — | Checked | | SLV-8 |
| Floating-point overflow to ±inf, NaN results | — | — | Checked, also in a narrowing conversion (`double` to `float`) | Inputs are assumed finite. A caller outside the analysis can still pass NaN or inf. | SLV-9, SLV-10, SLV-22 |
| Float to integer conversion that does not fit | — | — | Checked | | SLV-11 |
| Enum values | A switch with a case for every enumerator can take no case | Same path as Stone | A conversion outside the range of an enum without a fixed type is checked | An enum with a fixed type can hold any value of that type. Silver assumes inputs only within the enum's range. | STN-13, SLV-11, SLV-22 |
| Unsigned wraparound, lossy integer conversions | — | — | Checked only with `--silver-strict` | Both are defined behaviour in C++, so plain Silver allows them. | SLV-29, SLV-30 |
| Floating-point rounding and accuracy | — | — | — | Not covered. Write a `PORK_POST` with a bound to state an accuracy requirement. | |

## Hidden state, effects and code that runs

| Area | Stone | Bronze | Silver | Not covered, assumptions | Rules |
|---|---|---|---|---|---|
| Mutable globals, `static` and `thread_local` locals | Ruled out (constants allowed) | — | — | | STN-14, STN-22 |
| Side effects inside expressions, unsequenced writes | Ruled out | — | — | | STN-20, STN-21 |
| Code that runs implicitly: destructors, copies, user code inside std | Accounted for (each runs code that must be Stone) | — | User destructors and copy/move functions give `not silver` | | STN-67, STN-68, STN-72, STN-82, SLV-40 |
| Type punning: unions, `reinterpret_cast`, `const_cast` | Ruled out | — | — | `std::variant` is a trusted type. | STN-2, STN-19 |
| Virtual dispatch | Allowed when every known implementation is Stone | — | Only through an interface contract, with Liskov checks | Implementations outside the analysed sources are reported as the assumption `dispatch I::f`. | STN-50–56, SLV-34, SLV-37, SLV-38 |

## Data flow

| Area | Stone | Bronze | Silver | Not covered, assumptions | Rules |
|---|---|---|---|---|---|
| An output depends on an input it should not | — | Checked where `PORK_DEPENDS` is declared | Requires Bronze | Without a requirement nothing is promised. An allowed port covers everything its implementations can reach, also other arguments they hold references to. A requirement can be widened or removed; the verdict does not show that it changed. Only normal return is described. Overestimates count as dependencies. | BRZ-1–6 |
| Flow that cannot be computed (pointers, views, lambdas as values) | — | No Bronze for a function with a requirement | — | | BRZ-3 |

## Control flow and contracts

| Area | Stone | Bronze | Silver | Not covered, assumptions | Rules |
|---|---|---|---|---|---|
| Non-termination | — | — | Checked: every loop and recursion ends within `--unwind` | A function that needs more iterations is not Silver, so Silver means bounded. A function whose contracts lead back to itself is not Silver. | SLV-16, SLV-18 |
| Exceptions | Only `throw` of a `<stdexcept>` type with a literal; no try/catch | Normal return only | Checked: no reachable `throw` | Exceptions from the library itself (for example allocation failure) are not modelled. | STN-13, STN-83, SLV-43 |
| Preconditions at calls | Placement, and a pure condition | — | Checked at every analysed call | Callers outside the analysis are not checked. | STN-42, SLV-17, SLV-20, SLV-32 |
| Functional correctness | — | Dependencies only | As far as `PORK_POST`, `PORK_INVARIANT` and `assert` state it | Nothing beyond what the contracts say. | SLV-18, SLV-19, SLV-33 |

## Outside every level

These are not covered by any verdict.

- **Concurrency.** Pork analyses sequential code. Two threads that call
  Stone functions with the same `T&` can race.
- **Stack depth.** Recursion is bounded by `--unwind`, but stack size is not
  checked.
- **Resource exhaustion.** Allocation failure and other out-of-memory
  conditions are not modelled.
- **I/O, the operating system, time.** These are reachable only through
  trusted functions and contracts, so their behaviour is assumed.
- **Timing and side channels.**

## Conditions on every verdict

A verdict holds only under these conditions. They apply to every row
above.

- **The analysed build.** The verdict is for the source and compiler flags
  that Pork analysed. Annotations have no effect at run time. Code built
  with other flags, macros or `NDEBUG` settings, or code changed after the
  analysis, is not covered.
- **Trusted code.** Trusted functions and types (trust file,
  `pork::trusted`, the standard library) are assumed correct and are
  reported as assumptions. Silver also assumes that their C contracts are
  correct (SLV-21). Stone does not check the preconditions of trusted
  functions: `std::sort` with a comparator that is not a strict weak
  ordering is Stone and can overflow a buffer inside the library.
- **Library effects.** The loan model takes the effects of std functions
  from `summaries/std.txt`. A wrong line there is a wrong guarantee.
- **The Silver model.** Silver proves the generated C (SLV-2). It assumes
  that the translation preserves the C++ behaviour, as well as the input
  assumptions of SLV-22 and the bounds `--vector-bound` and `--unwind`.
- **What has a verdict.** Templates that are never instantiated and code
  with compile errors get no verdict. Lambda bodies are judged as part of
  the enclosing function.
