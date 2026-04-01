# Handover: tree-sitter-lean Parser Work On `main`

## Goal

Improve parsing of real Lean 4 files in `corpus/*.lean`, prioritizing real-file parsing over `test/` expectations when they disagree.

## Current Branch State

- Branch: `main`
- Still `5` commits ahead of `origin/main` (`db3d46c`)
- Working tree is dirty
- Current uncommitted code changes include:
  - `grammar.js`
  - `grammar/term.js`
  - `grammar/command.js`
  - `grammar/syntax.js`
  - `test/corpus/declarations.txt`
- Docs also changed:
  - `handover.md`
  - `plan.md`
  - `approach.md` is still untracked

## What Landed In This Round

### 1. `let rec ... := ...` support

`let_rec` in [grammar.js](/work/grammar.js) now accepts both:

- simple body:

```lean
let rec f ... := expr
body
```

- equation body:

```lean
let rec f ...
| pat => rhs
body
```

Important detail: both branches require the following `body` expression. Making the body optional caused a global top-level regression (`ERROR(module ...)`) and was reverted.

### 2. Equation arms split away from pattern-match arms

New internal category in [grammar/term.js](/work/grammar/term.js):

- `equation_alt`
- `_equation_alts`
- `_equation_alts_where_decls`

Wiring:

- `match` and `do_match` still use `_match_alts`
- `let_rec`, declaration equations, `let`/`have` equations, and `macro_rules` now use `_equation_alts`

Visible tree shape is preserved by aliasing `equation_alt` back to `match_alt`, so existing tests did not need broad tree updates.

### 3. Regression test added

Added a new passing corpus test in [test/corpus/declarations.txt](/work/test/corpus/declarations.txt):

- `Let Rec Simple Body`

## Current Results

### Tests

- `tree-sitter test`: `224` pass, `0` fail

### Corpus Status

| File | Previous | Now | Delta |
|------|----------|-----|-------|
| BigStep.lean | 6 | 6 | 0 |
| Misc.lean | 0 | 0 | 0 |
| Parser.lean | 55 | 55 | 0 |
| Pretty.lean | 0 | 0 | 0 |
| Runtime.lean | 0 | 0 | 0 |
| Syntax.lean | 0 | 0 | 0 |
| Typecheck.lean | 119 | 118 | **-1** |
| **Total** | **180** | **179** | **-1** |

### File-Level Parse Pass/Fail

- Passed: `4`
  - `Misc.lean`
  - `Pretty.lean`
  - `Runtime.lean`
  - `Syntax.lean`
- Failed: `3`
  - `BigStep.lean`
  - `Parser.lean`
  - `Typecheck.lean`

### Generator Metrics

Latest successful generate/build cycle after the new changes:

- Generate time: about `1m37s`
- Peak RSS: about `12.2 GB`
- `src/parser.c`: `157630820` bytes

State counts are still dominated by the usual broad expression rules:

- `binary_expression`: `18641`
- `comparison`: `16589`
- `_dollar`: `16361`
- `subarray`: `16224`
- `let_rec`: `10263`

The `_equation_alts` split did not create a new generator crisis by itself.

## Remaining Failure Modes

### 1. `if ... then ...` without `else` in do blocks

Example: [corpus/Parser.lean:86](/work/corpus/Parser.lean#L86)

```lean
if output.exitCode ≠ 0 then
  throw <| IO.userError s!"Parsing failed: {output.stderr}"
return output.stdout
```

This is still the highest-value remaining fix.

### 2. `return .Ctor arg...` in match arms

Example: [corpus/BigStep.lean:57](/work/corpus/BigStep.lean#L57)

```lean
| Expr.num n =>
  return .success config (Value.num n)
```

The parser still overextends the RHS and treats later material as application arguments.

### 3. Multiline `fun` with equation arms

Example: [corpus/Parser.lean:557](/work/corpus/Parser.lean#L557)

```lean
let contents := pexprContents.filterMap fun
  | Content.Element el => some el
  | _ => none
```

Later `|` arms still leak out of the intended `fun`.

## Experiments In This Round

### Worked

1. **Simple-body `let rec`**:
   - landed cleanly
   - added one new passing regression test
   - reduced `Typecheck.lean` by `1` error

2. **`_equation_alts` split**:
   - landed cleanly
   - no test regressions
   - no corpus regressions
   - removes one of the earlier blockers for future match-specific work

### Failed / Backed Out

1. **`do_if` via `_do_statement` + grammar refactor**

I tried a narrower `do_if` shape plus a new `_do_statement` path. Generation initially produced local unresolved conflicts, but resolving them with additional conflicts pushed `tree-sitter generate` into the same low-CPU timeout/stall mode described in the older notes.

Outcome:

- the `do_if` attempt was fully backed out
- current code does **not** contain `do_if`

2. **Optional `let_rec` body**

I initially allowed the new `:=` branch to omit the following body expression. That caused a broad parser regression where even simple files parsed as:

```text
(ERROR
  (module ...))
```

Tightening both `let_rec` branches to require a following body fixed the issue.

## What Changed About The Old Match-Token Blocker

The previous handover said match-token work was blocked because `_match_alts` was reachable through declaration/equation contexts such as `let_rec`.

That is now partly fixed:

- `let_rec` no longer uses `_match_alts`
- declaration equations no longer use `_match_alts`
- `let`/`have` equations no longer use `_match_alts`
- `macro_rules` no longer use `_match_alts`

What is still unresolved:

1. match-specific layout still needs a single-path grammar shape
2. scanner handling must avoid reintroducing the old indent-stack interference
3. `do_if` still blocks some of the same real-file cases upstream

## Recommended Next Work

### 1. Solve `do_if` via scanner-assisted boundary handling

Do **not** revive the backed-out `_do_statement` + conflict approach.

More promising direction:

- keep expression-level `if_then_else` unchanged
- use a DO-specific external token to mark the end of a `then` branch when the next same-column token is not `else`, or when the enclosing DO block closes past the branch

### 2. Add passing `do_if` tests only when the implementation is stable

Do not land red tests in the normal corpus.

Add minimal passing tests for:

- `if ... then ...` inside `do`
- `if ... then ... else ...` inside `do`

### 3. Revisit match-boundary work now that `_equation_alts` is in place

Any next match experiment should target only:

- `match`
- `do_match`

Avoid touching declaration/equation grammar in that experiment.

### 4. Defer `_by_*` scanner tokens

This is still lower priority than:

- `do_if`
- match-arm RHS boundaries
- multiline `fun` equation handling

## Useful Commands

```bash
# Generate with realistic current limits
/usr/bin/time -v timeout 180s prlimit --as=17179869184 -- tree-sitter generate --report-states-for-rule -

# Build parser
tree-sitter build

# Run tests
tree-sitter test

# Count ERROR nodes per corpus file
for f in corpus/*.lean; do
  n=$(tree-sitter parse "$f" 2>/dev/null | rg -c 'ERROR \[')
  printf '%s,%s\n' "$(basename "$f")" "${n:-0}"
done

# File-level pass/fail
./scripts/parse-dir.sh -j 4 corpus
```

## Existing Stashes

- `stash@{0}`: `wip parser triage current diffs`
- `stash@{1}`: `wip apply-head experiment`

Do not pop them blindly.
