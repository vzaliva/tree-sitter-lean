# Handover: tree-sitter-lean Parser Work On `main`

## Goal

Improve parsing of real Lean 4 files in `corpus/*.lean`, prioritizing real-file parsing over `test/` expectations when they disagree.

## Current Branch State (2026-03-13)

- Branch: `main`
- Commit base: `db3d46c` (`origin/main`)
- Current uncommitted diff: only [grammar/do.js](/work/grammar/do.js)
- No commits made in this session

### Active Local Change

The only live source change is:

```diff
do_return: $ => prec.left(PREC.lead,
-  seq('return', optional(field('value', $._expression))),
+  seq('return', field('value', prec(PREC.lead, $._expression))),
),
```

This is a real behavioral change, not just formatting. It makes `return` require a value and gives that value stronger precedence.

## Build / Environment Notes

- `tree-sitter generate` works reliably only **without** `prlimit` in this environment.
- Even after the user allowed `20G`, this command still segfaulted immediately here:

```bash
/usr/bin/time -v timeout 180s prlimit --as=20G tree-sitter generate --report-states-for-rule -
```

- The command that actually works is:

```bash
/usr/bin/time -v timeout 180s tree-sitter generate --report-states-for-rule -
```

- Current generate characteristics on this machine:
  - wall time: about `1m40s` to `1m44s`
  - peak RSS: about `12.8 GB`
- After changing grammar or scanner sources, I ran:

```bash
tree-sitter build
```

## Current Corpus Status vs Committed `main`

Counts below are number of `ERROR` nodes from:

```bash
tree-sitter parse corpus/<file>.lean | rg -o 'ERROR \[' | wc -l
```

### Current Working Tree (`do_return` change live)

| File | Error nodes |
|------|-------------|
| BigStep.lean | 8 |
| Misc.lean | 0 |
| Parser.lean | 53 |
| Pretty.lean | 0 |
| Runtime.lean | 0 |
| Syntax.lean | 53 |
| Typecheck.lean | 121 |

Total: **235**

### Committed `main` (`db3d46c`)

| File | Error nodes |
|------|-------------|
| BigStep.lean | 8 |
| Misc.lean | 0 |
| Parser.lean | 52 |
| Pretty.lean | 0 |
| Runtime.lean | 0 |
| Syntax.lean | 53 |
| Typecheck.lean | 121 |

Total: **234**

### Comparison Summary

- Net corpus delta vs committed `main`: **worse by 1 error node**
- The only measured corpus difference was:
  - `Parser.lean`: `52 -> 53`
- `Syntax.lean` coverage: **no change** (`53 -> 53`)
- `BigStep.lean`: **no change** (`8 -> 8`)
- `Typecheck.lean`: **no change** (`121 -> 121`)

### File-Level Parse Pass/Fail (`./scripts/parse-dir.sh corpus`)

This did **not** change between committed `main` and the current working tree:

- Passed: `4`
- Failed: `3`
- Failing files:
  - `BigStep.lean`
  - `Parser.lean`
  - `Typecheck.lean`

Important: `parse-dir.sh` only uses the parser exit code, so `Syntax.lean` shows as `PASS` there even though it still contains many `ERROR` nodes. Use the error-node counts above for real coverage tracking.

## What Actually Improved

The `do_return` change is still worth knowing about because it fixes one real ambiguity, even though it did not improve corpus totals.

### Fixed Minimal Repro

This now parses correctly:

```lean
def f : IO Unit := do
  if fuel = 0 then
    return a
  else
    match expr with
    | x =>
      return y
    | z =>
      return w
```

On committed `main`, this shape misparsed. With the local `do_return` change, it parses cleanly.

## Main Remaining Failing Shapes

### 1. Multiline `fun` with equation arms inside an argument

Still fails:

```lean
def flat : Except String Typ := do
  let contents := content.filterMap fun
    | a => some a
    | b => some b
    | _ => none
  match contents with
  | _ => pure ()
```

Observed failure mode:

- only the first `fun` arm stays inside the `fun`
- later `| ... => ...` arms leak out and get parsed as outer `match_alt`s / surrounding structure

### 2. `if ... else match` in `do`, where the match-arm body is `return .Ctor arg...`

Still fails:

```lean
def f : IO EvalResult := do
  if fuel = 0 then
    return .outOfFuel
  else
    match expr with
    | Expr.num n =>
      return .success config (Value.num n)
    | Expr.bool b =>
      return .success config (Value.bool b)
```

Observed failure mode:

- the parser closes the first match-arm body too early
- after `return .success`, the remaining `config (...)` gets treated as outer application material
- the next sibling `|` arm then falls into error recovery

### 3. `Syntax.lean` cascade around line ~370

`Syntax.lean` is unchanged relative to committed `main`:

- still `53` error nodes
- still cascades from the same region around lines `369` to `388`

## Experiments Tried In This Session

These were tested and **reverted** unless noted otherwise.

### Kept

1. `do_return` requires a value

- File: [grammar/do.js](/work/grammar/do.js)
- Result:
  - fixes the smaller `if ... else match` repro shown above
  - does **not** improve corpus totals
  - currently the only live diff

### Reverted: grammar experiments

1. `match_alt` RHS high precedence

- Change: wrapped `match_alt` RHS in `prec.right(PREC.max, ...)`
- Result:
  - no meaningful improvement on the two main repros
  - reverted

2. `fun` equation-arm RHS high precedence

- Change: wrapped equation-style `fun` arm RHS in `prec.right(PREC.unary, ...)`
- Result:
  - no improvement on the multiline `fun` repro
  - reverted

3. Add `if_then_else` to `term` / exclude it from generic apply head

- Result:
  - contributed to broader parse distortions
  - reverted

4. Tokenized `dot_identifier` / dotted apply-head experiments

- Variants tried:
  - regex/token `dot_identifier`
  - dedicated dotted apply head in `_apply`
  - excluding `cdot` from generic apply head
- Result:
  - some variants broke qualified names like imports / dotted identifiers
  - other variants distorted normal applications without fixing the real corpus issue
  - reverted

### Reverted: scanner experiments

1. Stop suppressing `pending_newline` before `|`

- Hypothesis:
  - sibling match arms after an indented arm body need that newline after dedent
- Result:
  - helped the tiny local multiline-arm shape
  - catastrophically worsened corpus parses
  - reverted

2. Add unused external error-recovery sentinel

- Based on Tree-sitter docs suggesting scanners should opt out during recovery
- Result:
  - changed recovery behavior, but not for the better here
  - `BigStep.lean` became noisier
  - reverted

## Interpretation

### Compared to committed `main`

- There is **no net syntax coverage progress** right now.
- The current local diff is best understood as a **targeted ambiguity fix** that helps one specific `do`-block shape, but not the corpus.

### What this implies

- If the goal is strictly “improve corpus coverage before anything else”, the current `do_return` change probably should **not** be kept unless paired with another change that more than compensates for `Parser.lean +1`.
- If the goal is to keep a local fix while investigating the larger issues, the current branch state is safe enough to continue from, but it is not a measurable corpus improvement over committed `main`.

## Suggestions For Next Work

1. Treat the `fun` equation-arm leak and the `return .Ctor arg...` match-arm failure as **separate problems**.

2. For the multiline `fun` leak:
   - focus on why equation-style `fun` arms are losing ownership of later `|` lines
   - likely avenues:
     - explicit separators / indentation-aware handling for equation-style `fun`
     - a narrower rule for equation-lambda bodies instead of plain `$._expression`

3. For the `return .Ctor arg...` issue:
   - the root tension still looks like the interaction between:
     - `do_return`
     - match-arm body boundaries
     - scanner `|` suppression after multiline bodies
   - a successful fix probably needs more context than a blanket precedence tweak

4. If revisiting the scanner:
   - avoid broad changes to `|` suppression
   - test every scanner tweak immediately against:
     - the two minimal repros
     - `BigStep.lean`
     - `Parser.lean`
     - `Syntax.lean`

5. Keep measuring with error-node counts, not only parser exit status.

## Useful Commands

```bash
# Generate and inspect rule state counts
/usr/bin/time -v timeout 180s tree-sitter generate --report-states-for-rule -

# Rebuild parser
tree-sitter build

# Count ERROR nodes per corpus file
for f in corpus/*.lean; do
  n=$(tree-sitter parse "$f" 2>/dev/null | rg -o 'ERROR \[' | wc -l)
  printf '%s,%s\n' "$(basename "$f")" "$n"
done

# Quick file-level pass/fail summary
./scripts/parse-dir.sh corpus
```

## Existing Stashes

These were left in place:

- `stash@{0}`: `wip parser triage current diffs`
- `stash@{1}`: `wip apply-head experiment`

Do not pop them blindly; both contain reverted experiments that did not clearly improve corpus parsing.
