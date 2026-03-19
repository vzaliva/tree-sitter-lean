# Handover: tree-sitter-lean Parser Work On `main`

## Goal

Improve parsing of real Lean 4 files in `corpus/*.lean`, prioritizing real-file parsing over `test/` expectations when they disagree.

## Current Branch State

- Branch: `main`
- 4 commits ahead of `origin/main` (`db3d46c`)
- No uncommitted changes

### Commits Made

1. `2634921` — Clean up dead code, remove orphan try/catch/finally, make do_return value optional
2. `54bb3f2` — Context-aware layout scanner with DO_OPEN/DO_SEPARATOR/DO_CLOSE tokens
3. `347570f` — Remove incorrect DO_SEPARATOR fallback on indent increase
4. `ddc4ec1` — Remove unnecessary conflict declarations

## Architecture Changes

### Context-Aware Scanner (`src/scanner.c`)

The scanner now uses a **context-tagged indent stack**. Each entry tracks both column position and context type:

```c
typedef struct {
    uint16_t column;
    uint8_t context;  // CONTEXT_GENERIC or CONTEXT_DO
} IndentEntry;
```

**New external tokens** (added to generic `_newline`/`_indent`/`_dedent`):
- `_do_open` — pushes CONTEXT_DO when `valid_symbols[DO_OPEN]` is set (grammar requests it only in `_do_seq`)
- `_do_separator` — emitted at same-column (colEq) in DO context
- `_do_close` — emitted on dedent from DO context

**Key scanner behaviors**:
- `|` suppression applies to both `_newline` AND `_do_separator` (needed for match arms inside do blocks)
- DO_SEPARATOR is NOT emitted as fallback on indent increase (only at colEq)
- Multi-level dedent pops one stack entry per scanner call (preserves context for correct token emission)

### Grammar Changes (`grammar.js`, `grammar/do.js`)

- `_do_seq` now uses `seq($._do_open, sep1($._do_element, $._do_separator), $._do_close)` instead of `seq($._indent, sep1($._do_element, $._newline), $._dedent)`
- `do_return` value is optional (bare `return` now works)
- `_do_expression` alias removed; `_expression` used directly in `_do_element`
- Dead code removed from `do.js` (100+ lines of unreachable old rules)
- Orphan `try`/`catch`/`finally` rules removed from `grammar.js`
- Several unnecessary conflict declarations removed

## Current Results

### Tests: 223 pass, 0 fail

### Corpus Status

| File | Origin/main | Now | Delta |
|------|-------------|-----|-------|
| BigStep.lean | 8 | 6 | **-2** |
| Misc.lean | 0 | 0 | 0 |
| Parser.lean | 53 | 55 | +2 |
| Pretty.lean | 0 | 0 | 0 |
| Runtime.lean | 0 | 0 | 0 |
| Syntax.lean | 53 | **0** | **-53** |
| Typecheck.lean | 121 | 119 | **-2** |
| **Total** | **235** | **180** | **-55 (-23%)** |

### File-Level Parse Pass/Fail

- Passed: **4** (was 3) — Misc, Pretty, Runtime, **Syntax** (new)
- Failed: 3 — BigStep, Parser, Typecheck

## Remaining Failure Modes

### 1. `return .Ctor arg...` in match arms (BigStep.lean lines 49-60)

```lean
| Expr.num n =>
  return .success config (Value.num n)
```

The parser greedily extends `return` past `.success` and consumes `config (Value.num n)` as application arguments. Needs match-arm-aware layout to bound the RHS.

### 2. `if ... then ...` without `else` in do blocks

```lean
if output.exitCode ≠ 0 then
  throw <| IO.userError "fail"
-- no else branch
```

`if_then_else` requires `else`. Adding a separate `do_if` with optional else is **blocked by tree-sitter generator hang** on the conflict `[$._do_element, $.if_then_else]`. Making `else` optional globally causes regressions elsewhere.

### 3. Multiline `fun` with equation arms (Parser.lean lines 557-561)

```lean
let contents := pexprContents.filterMap fun
  | Content.Element el => some el
  | _ => none
```

Later `|` arms leak out of the fun expression. Needs fun-specific or match-specific layout tokens.

## Experiments Tried and Results

### Worked

1. **Context-aware DO scanner** — Major win. Syntax.lean fully fixed.
2. **Optional `do_return` value** — Fixed "Return Nothing" test.
3. **Removing DO_SEPARATOR fallback on indent increase** — Fixed several BigStep/Typecheck regressions.

### Didn't Work

1. **`do_if` as separate rule** — tree-sitter generator hangs on `[$._do_element, $.if_then_else]` conflict declaration. Appears to be a tree-sitter bug with certain conflict patterns.
2. **Optional `else` in `if_then_else`** — Fixes target case but causes +14 net regression (dangling-else ambiguity in non-do contexts, state explosion 2m37s/17GB).
3. **`_do_statement` category** — 4x state explosion (6m10s, 25.8 GB) from parallel choice set overlapping with `_expression`.
4. **`_do_expr` with term.forbid** — Too complex, naming conflicts with existing `_do_term`.

## Approach Documents

- `/work/approach.md` — Root cause analysis and recommended approach
- `/work/plan.md` — Implementation plan (Phase 0-4)

## Suggestions For Next Work

1. **Match-specific layout tokens** (`_match_open`/`_match_separator`/`_match_close`): Would address failure modes 1 and 3. Requires restructuring `_match_alts` from `repeat1(match_alt)` to `seq(_match_open, sep1(match_alt, _match_separator), _match_close)`. The `|` suppression hack could then be replaced with context-aware logic.

2. **`do_if` via scanner token**: Instead of a grammar-level `do_if` rule, emit a special scanner token (e.g., `_do_if_end`) when an `if ... then ...` block should terminate without `else` in DO context. This avoids the tree-sitter conflict entirely.

3. **`by` tactic block tokens**: Add `_by_open`/`_by_separator`/`_by_close` similar to the DO tokens. `tactics` currently uses generic `_newline` separator.

## Useful Commands

```bash
# Generate with resource limits
/usr/bin/time -v timeout 180s tree-sitter generate --report-states-for-rule -

# Build parser
tree-sitter build

# Run tests
tree-sitter test

# Count ERROR nodes per corpus file
for f in corpus/*.lean; do
  n=$(tree-sitter parse "$f" 2>/dev/null | rg -o 'ERROR \[' | wc -l)
  printf '%s,%s\n' "$(basename "$f")" "$n"
done

# File-level pass/fail
./scripts/parse-dir.sh corpus
```

## Existing Stashes

- `stash@{0}`: `wip parser triage current diffs`
- `stash@{1}`: `wip apply-head experiment`

Do not pop them blindly; both contain reverted experiments from before this session.
