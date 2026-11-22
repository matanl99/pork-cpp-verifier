# Confidence in Pork verification results

A verdict is useful if its guarantee holds for the original C++ program.
Silver's prover checks generated C. Carrying that result back to C++ depends
on Stone enforcing the assumptions made by the translator, the translation
preserving the relevant behavior, and the library models and contracts
holding under the reported assumptions. The matrices show what could
falsify each guarantee and where the current checks leave gaps.

## Pork Stone

| Rule or property | What acceptance means | Evidence and ways to challenge it | Remaining limits |
|---|---|---|---|
| Initialization (STN-15–17) | A value is initialized before it is read. | The runtime oracle uses MemorySanitizer on eligible original C++ examples. Exercise paths that skip a required assignment and compare the runtime report with the Stone verdict. | Coverage depends on the executed paths and instrumentation. The MSan harness skips cases with standard-library headers. |
| Loans and lifetimes (STN-62) | A loan remains valid whenever it is used. | The runtime and library-summary oracles exercise original C++ with sanitizers and libstdc++ debug checks. Try both an insertion that reallocates a vector and one that leaves its storage in place, with the borrowed element used after the insertion. A self-test changes the summary for `push_back` and checks that the oracle finds a false acceptance. | Instrumentation detects particular kinds of invalid use on executed paths. Library models also depend on the operations and implementations covered. |
| Aliasing and effects (STN-20–23, STN-62, STN-82) | Accepted calls respect the alias rules, and analysis accounts for the user code that executes. | The rule corpus covers accepted and rejected calls. A further check would compare actual writes in original C++ with the effects Pork assigns to calls, including implicit copies and destructors. | Concrete runs can expose a missed effect. Coverage of all call paths and implicit operations remains a separate obligation. |

[Stone specification](../spec/1_stone.txt) · [Runtime oracle](../tests/check_runtime.py) · [Library-summary oracle](../tests/check_summaries.py)

## Pork Bronze

| Rule or property | What acceptance means | Evidence and ways to challenge it | Remaining limits |
|---|---|---|---|
| Declared dependencies (BRZ-1–3) | An output covered by `PORK_DEPENDS` depends only on the permitted inputs. | The existing flow oracle compares two executions in generated C. A proposed original-C++ check would hold allowed inputs equal, vary an excluded input, and compare the specified output on two admissible normal returns. A difference falsifies the requirement for that pair. | The existing oracle shares the translation with Silver. Concrete C++ comparisons cover only the input pairs exercised. Functions without declared requirements make no additional dependency promise. |
| Control flow and retained output values (BRZ-2) | Branches that select an output value and paths that retain its incoming value contribute dependencies when relevant. | The flow corpus checks expected dependencies. Original-C++ pairs can exercise changed and unchanged outputs, including a branch that leaves an output untouched. The oracle self-test deliberately drops dependencies to check that its comparison detects omissions. | Input pairs must respect contracts and differ in a way that isolates the dependency under test. |
| Enforcement (BRZ-3–6) | Unavailable flow analysis or a violated declared requirement prevents Bronze and Silver. | Regression cases check verdicts and exit status, including a violated flow requirement in a Silver run. | This establishes enforcement for exercised cases. The accuracy of inferred dependencies still depends on the flow analysis. |

[Bronze specification](../spec/3_bronze.txt) · [Flow tests and oracle](../tests/check_flow.py)

## Pork Silver

| Rule or property | What acceptance means | Evidence and ways to challenge it | Remaining limits |
|---|---|---|---|
| C++ to C translation (SLV-2) | The generated model preserves the relevant C++ behavior. | The equivalence prototype compares return values and mutations of reference arguments. Extend it with concrete comparisons of original C++ and generated C over small, exhaustive input domains. Prioritize destructors, library operations, and boundary values. | The prototype shares harness assumptions and relies on ESBMC's C++ models. The notes record differences that were not confirmed in concrete runs. Concrete agreement covers the tested domain. |
| Runtime checks (SLV-5–15) | The checks in the model establish absence of the listed errors under the function's preconditions and reported assumptions. | Use corpus cases on both sides of arithmetic, conversion, and indexing boundaries. For a suspected false Silver, check an admissible input against the original C++ with suitable instrumentation and compare it with the checks in retained C. | A second prover on the same C can catch prover discrepancies. Both runs still share the translation and models. Instrumentation in original C++ covers selected errors. |
| Unwinding and preconditions (SLV-16–17) | Admissible executions satisfy the unwinding checks, and preconditions are checked at analyzed call sites. | Use cases that cross the unwinding bound and cases with calls that violate a callee's precondition. A proposed concrete comparison would exercise inputs on both sides of each boundary. | The evidence depends on the harness admitting the intended inputs and on the reported model bounds and trusted assumptions. |

[Silver specification](../spec/2_silver.txt) · [Equivalence prototype and limitations](equivalence.md) · [Silver corpus example](../corpus/20_silver.cpp)

## Recording evidence

Record the rule ID, test link, commit and tool versions, representation
checked, input domain, expected verdict, and observed result. Include a
reproducer for any discrepancy. Keep three kinds of evidence visible.

- **Regression tests** show that Pork produced the expected verdict for
  particular examples.
- **Independent checks** compare a verdict with another analysis or against
  the original C++ behavior. Record which parts of the implementation the
  comparison shares with Pork.
- **Deliberately introduced faults** show whether a check catches a specific
  failure. Examples include removing a loan invalidation, dropping a data
  dependency, or omitting a destructor in the translation.

Start with rules whose failure could give a false Silver verdict, including
destructors, loans across container mutations, and integer conversions. A
confirmed counterexample can disprove a guarantee. Stronger general
confidence also requires explaining why the relevant rules, translation steps,
and library models preserve it.

## How Pork checks rules from the Clang AST

Clang parses the source with the project's compiler flags and produces a typed
abstract syntax tree (AST). It identifies the kind and type of each expression,
the declarations that names refer to, and the targets of direct calls.
Pork's AST visitor gathers facts about each function definition, including
its callees and annotations. Function templates are classified per
instantiation. [ClangFactsSource.cpp](../adapters/clang/ClangFactsSource.cpp)

### Pork Stone

The local checker examines AST nodes in context. For example, STN-20 permits
an assignment at statement level and rejects one nested inside a return
expression:

```cpp
x = y + 1;
return (x = y);
```

Clang represents both assignments as a `BinaryOperator`. The first reaches
`exprStmt`, which checks the assignment and its operands. The second reaches
`expr` through the return statement and is rejected with
`side effect inside expression`. [LocalCheck.cpp](../adapters/clang/LocalCheck.cpp)

The loan checker builds a control-flow graph (CFG) with Clang. It follows
where a reference came from, whether it is still live, and how statements
and calls affect what it borrows. For example, if `p` refers to a vector
element and remains live after a `push_back` that may invalidate it, the
loan checker can reject the function. A Stone verdict requires both the
local rules and the loan model to accept the function. Classification also
propagates rejections through the call graph and records trust assumptions.
[LoanCheck.cpp](../adapters/clang/LoanCheck.cpp) · [Classify.cpp](../core/Classify.cpp)

### Pork Bronze

Pork builds its own data-flow representation from the AST and checks
declared dependencies against inferred ones. Consider a function with
`PORK_DEPENDS_RESULT(a)` and `return flag ? a : b;`. The result can depend on
`b`, which supplies the value on one branch, and on `flag`, which selects
the branch. The declared requirement allows only `a`, so the function
fails Bronze. The core checks declared requirements before Silver.
`--flow` displays the inferred dependencies.
[FlowBuild.cpp](../adapters/clang/FlowBuild.cpp) · [Flow.cpp](../core/Flow.cpp) · [Analyse.cpp](../core/Analyse.cpp)

### Pork Silver

For `return 100 / x;`, the AST identifies the division and connects the
denominator to the parameter declaration. Pork translates the function to C
and builds a harness that lets the inputs vary under the preconditions. With
`PORK_PRE(x > 0)`, the prover checks for division by zero over the inputs the
harness admits. The `--keep-c` option lets a reader inspect the actual program
passed to the prover, including its assumptions and checks.
[CTranslate.cpp](../adapters/clang/CTranslate.cpp) · [Silver.cpp](../core/Silver.cpp)

To review a verdict, start at the AST rule or the loan analysis for Stone.
For Bronze, trace dependencies through expressions, branches, and calls.
For Silver, also compare the retained C with the original C++. A successful
proof of the generated C can coexist with a translation that omits a
relevant C++ effect. The equivalence checks described above target that gap.
