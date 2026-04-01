# Plan: Next Parser Work After `let rec :=` and `_equation_alts`

## Summary

The safe grammar slice is now done:

- `let rec ... := ...` is supported
- equation-style arms are split away from pattern-match reachability via `_equation_alts`
- tests are green again

The remaining work is still blocked on `do_if` and match/fun boundary handling. The order below reflects what is now true in the repo, not the earlier draft.

## Current Validated State

- `tree-sitter test`: `224/224` passing
- `./scripts/parse-dir.sh -j 4 corpus`: `4/7` passing
- Current corpus error counts:
  - `BigStep.lean`: `6`
  - `Parser.lean`: `55`
  - `Typecheck.lean`: `118`
  - all others: `0`
- Current generated parser metrics:
  - generate time: about `1m37s`
  - peak RSS: about `12.2 GB`
  - `src/parser.c`: `157630820` bytes

## Completed Work

Do not redo these:

- DO-specific scanner layout is already in place:
  - `_do_open`
  - `_do_separator`
  - `_do_close`
- `do_return` already accepts a bare `return`
- `let rec` now supports both:
  - `let rec f ... := expr`
  - `let rec f ... | pat => body`
- Added a regression test for simple-body `let rec` in `test/corpus/declarations.txt`
- Introduced `_equation_alts` and `_equation_alts_where_decls`
  - declaration equations, `let`/`have` equations, `let_rec`, and `macro_rules` now use `_equation_alts`
  - `match` and `do_match` still use `_match_alts`
  - equation alts are aliased back to `match_alt` so the visible tree shape stays stable

## Remaining Failure Classes

### 1. `if ... then ...` without `else` in `do`

Example: `corpus/Parser.lean:86`

This is still the highest-value remaining fix. The pure grammar approach is still blocked:

- adding `do_if` through a new `_do_statement` path required new conflicts
- those conflicts pushed generation into the known low-CPU timeout path
- that attempt was backed out

### 2. Match-arm RHS overconsumption

Example: `corpus/BigStep.lean:57`

`return .success config (Value.num n)` still absorbs too much as application.

### 3. Multiline `fun` equation leakage

Example: `corpus/Parser.lean:557`

`fun` equation arms still leak into surrounding structure.

## Next Execution Order

### Phase 1: Solve `do_if` without reintroducing overlap

Do this next.

Constraints:

- do not reintroduce a broad `_do_statement` that overlaps heavily with `_expression`
- do not rely on conflict declarations as the main mechanism
- keep expression-level `if_then_else` unchanged for non-`do` contexts

Recommended direction:

- treat `do_if` as a scanner-assisted boundary problem, not just a grammar-choice problem
- prototype a DO-specific token that marks the end of a `then` branch when:
  - the branch body has ended
  - the next same-column token is not `else`
  - or the enclosing DO context dedents past the branch
- only wire this token into the grammar where `do_if` expects it

Acceptance criteria:

- generator completes without new conflict-driven stalls
- a minimal `do if`-without-`else` case parses
- expression `if_then_else` tests remain green
- `Parser.lean` error count drops

### Phase 2: Add passing repro tests for the `do_if` shape

Do this only when the grammar change is stable.

Add minimal passing tests for:

- `if ... then ...` inside `do` with no `else`
- `if ... then ... else ...` inside `do`
- nested `else if` if the implementation supports it

Do not land red tests in the passing corpus.

### Phase 3: Revisit match-arm boundaries using `_equation_alts`

Now that `_match_alts` is no longer shared with declarations and `let_rec`, match-specific work is less risky.

Next experiment should target only:

- `match`
- `do_match`

Avoid touching declaration-equation grammar in this phase.

Goals:

- stop `return .Ctor ...` from absorbing sibling material
- preserve current tree shape for existing match tests

### Phase 4: Address multiline `fun` equation arms

Only after match-boundary work is under control.

Possible directions:

- dedicated `fun`-arm boundary handling
- a restricted RHS category for equation-style `fun`
- scanner support if grammar-only changes keep leaking

Do not start with `fun` before `do_if` and match work, because the current failures suggest those boundaries are more foundational.

### Deferred

Keep these deferred until the above is done:

- `_by_open/_by_separator/_by_close`
- broad `_expression` rewrites not tied to a reproduced failure
- global `else` optionality in `if_then_else`
- reviving the old `_do_statement` conflict-based attempt

## Verification Checklist

Run after every phase:

```bash
/usr/bin/time -v timeout 180s prlimit --as=17179869184 -- tree-sitter generate --report-states-for-rule -
tree-sitter build
tree-sitter test
./scripts/parse-dir.sh -j 4 corpus
for f in corpus/*.lean; do
  n=$(tree-sitter parse "$f" 2>/dev/null | rg -c 'ERROR \[')
  printf '%s,%s\n' "$(basename "$f")" "${n:-0}"
done
wc -c src/parser.c
```

Hard stop conditions:

- generator hangs or drops into the low-CPU stall mode
- new broad conflicts are required just to generate
- tests regress
- parser size or RSS jumps materially without corpus improvement
