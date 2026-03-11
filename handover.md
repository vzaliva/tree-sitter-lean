# Handover: tree-sitter-lean Indentation-Sensitive Parsing

## Goal

Parse real Lean 4 files correctly by adding indentation-sensitive parsing (INDENT/DEDENT) to the tree-sitter-lean grammar. Target: all 7 `.lean` files in `corpus` folder parse with zero errors.

## Current Status (2026-03-11)

- **223/223 corpus tests pass**
- **Parser state count: 45164**
- **Grammar generates cleanly** — no state explosion risk in current state
- **All changes uncommitted** on branch `positional`

### Corpus File Error Counts (234 total, down from 267 baseline = 12.4% reduction)

| File | Errors | Notes |
|------|--------|-------|
| BigStep.lean | 8 | Mostly `|`-suppression / match arm issues |
| Misc.lean | 0 | Clean |
| Parser.lean | 52 | Cascading from do blocks in equation-style match arms |
| Pretty.lean | 0 | Clean |
| Runtime.lean | 0 | Clean |
| Syntax.lean | 53 | All cascade from ~line 370, do blocks in equation arms |
| Typecheck.lean | 121 | Dominated by `if-without-else` pattern (~60%) |

## What Changed (Sessions 1–3 Combined)

### 1. Scanner Rewrite (`src/scanner.c`)

Python-style whitespace consumption. Scanner loops through whitespace itself rather than relying on `lexer->lookahead == '\n'` (which fails when tree-sitter extras consume newlines first).

- Consumes `\n`, `\r`, ` `, `\t` with `advance(lexer, true)`
- Uses `lexer->get_column(lexer)` to measure indent after whitespace
- `|` suppression: NEWLINE suppressed when `lookahead == '|'` at 3 sites (pending_newline line 32, indent>current line 99, indent==current line 109)
- `pending_newline` flag set after DEDENT when indent matches new stack top
- Indent stack: `uint16_t[100]`, initialized with `[0]`, depth=1

### 2. Grammar Changes

**`grammar.js`:**
- `externals`: `$._newline`, `$._indent`, `$._dedent`
- `_do_seq`: `choice(seq($._indent, sep1($._do_element, $._newline), $._dedent), $._do_element)`
- `do`: `prec.right(seq('do', $._do_seq))`
- `let_mut`: `prec(100, ...)` to win over `do_let`
- Added `∈`, `∉`, `⊆`, `⊂`, `∩`, `∪` to comparison operators
- Added conflicts: `[$.do_match]`, `[$._expression, $._do_element]`, `[$.do_let, $.let]`, `[$.do_let, $.let_rec]`, `[$.do_let, $.parameters]`, `[$.do_let, $.let_mut]`

**`grammar/command.js`:**
- `theorem` accepts `choice('theorem', 'lemma')`
- `_decl_modifiers` includes `'nonrec'`
- Added `opaque_decl`: `seq('opaque', $._decl_id, $._decl_sig, optional($._decl_val_simple))`
- Added `set_option`: `seq('set_option', $.identifier, choice($.number, $.identifier, $.string, $.true, $.false))`

**`grammar/do.js`:**
- `do_let` rewritten: expanded name to `choice($.identifier, $.hole, $.parenthesized, $.anonymous_constructor)`, added optional parameters and type annotation, no prec hacks
- `_do_element`: `choice($.do_match, $.do_let, $._do_expression, $.assign, $.for_in, $.let_bind, $.let_mut, $.do_return)`

**`grammar/tactic.js`:**
- `simp` accepts `choice('simp', 'simp_all')` with optional `'only'`
- Added: `split_ifs`, `omega`, `decide`, `assumption`, `contradiction`, `constructor_tactic`
- Tactic separator: `sep1_($._tactic, seq(optional(';'), $._newline))`

**`grammar/term.js`:**
- `structure_instance` supports indented fields: `seq($._indent, sep1(field, choice(',', $._newline)), optional(','), $._dedent)`
- `_where_decls` supports indented form: `seq('where', $._indent, sep1(...), $._dedent)`

### 3. Test Changes

- `test/corpus/do.txt`: Updated `let` → `do_let` in "Let (Bind)" and "Let Annotated" test expectations

## Remaining Issues (Prioritized)

### 1. `if-without-else` in do blocks (INTRACTABLE)

**Impact**: ~60% of Typecheck.lean errors (73+ of 121). The pattern `if cond then action` (no `else`) is extremely common in Lean monadic code.

**Why it's hard**: Every approach to add `do_if` (if-without-else) causes state explosion:
- `do_if` with `_do_seq` branches → recursive cycle through `_do_element` → segfault (45GB+)
- `do_if` with `_expression` branches → still explodes via `_expression → do → _do_seq → _do_element`
- `do_if` with `_term` branches → still explodes
- `do_if` with `$.apply` branches → still explodes
- Optional `else` on existing `if_then_else` → 63K states, segfaults

**Root cause**: The dangling-else ambiguity combined with the recursive `_do_element → _do_expression → _expression` path creates exponential state space. Any form of `if` without mandatory `else` is fundamentally intractable with the current grammar structure.

**Possible future approaches**:
- External scanner emitting `DO_IF_END` at dedent boundaries
- Restricting `_do_expression` to exclude constructs that contain `_do_seq`
- Completely separate do-block expression grammar (massive refactor)

### 2. `|` Suppression Tension

**Impact**: BigStep.lean (8 errors), Syntax.lean (53), Parser.lean (52)

The scanner suppresses NEWLINE when `lookahead == '|'` at 3 sites. This is needed for match arm continuations (prevents splitting `match x with\n| a => ...` into separate statements). But it's harmful for:
- Equation-style function definitions with `|` arms
- Inductive constructors starting with `|`
- Do blocks inside match arms where the next `|` is a sibling arm, not a continuation

**Experiments tried**: Removing ANY of the 3 suppression sites worsens overall error counts. All combinations tested:
- Remove pending_newline suppression: Typecheck 121→128
- Remove indent>current suppression: Typecheck 121→144, BigStep 8→10
- Remove all suppression: Typecheck 121→161, BigStep 8→10

**Root cause**: The scanner has no syntactic context — it can't distinguish "this `|` is a match arm of the current match" from "this `|` is an inductive constructor or equation arm at the outer level."

### 3. Cascading Errors

- **Syntax.lean (53 errors)**: All cascade from ~line 370 where do blocks appear inside equation-style match arms. Once the first `|` arm fails, all subsequent arms misparse.
- **Typecheck.lean (121 errors)**: The entire 1853-line file gets wrapped in ERROR due to cascading from early `if-without-else` failures.
- **Parser.lean (52 errors)**: Mix of do-in-match-arms and unsupported patterns.

### 4. `<;>` Tactic Combinator (INTRACTABLE)

Adding `<;>` as a tactic separator/combinator causes state explosion (43K→61K states) because it tokenizes as three separate tokens `<`, `;`, `>`, creating massive ambiguity with comparison operators.

### 5. `do_let` vs `let` Precedence

`let` (expression with body) is still reachable through `_do_expression → _expression → let` and steals the `_newline` token. The regular `let` consumes `choice($._newline, ';')` then greedily eats the next expression as body. Result: `let x := 5\nlet y := 6\nreturn x` parses as nested `let` expressions, not separate `_do_element`s.

This is partially mitigated by the `[$.do_let, $.let]` conflict + GLR, but not fully resolved.

## Lessons Learned

1. **State explosion is the primary constraint.** The grammar is at ~45K states. Any addition that increases ambiguity by even a modest amount can push it to 60K+ or cause OOM. Always use the guarded generate command.

2. **`tree-sitter generate` IS deterministic.** Same grammar always produces same state count. No need to retry — if it explodes, the grammar needs changing.

3. **Always use guarded generation:**
   ```bash
   /usr/bin/time -v timeout 60s prlimit --as=8G tree-sitter generate --report-states-for-rule -
   ```

4. **Cascading errors dominate.** Most corpus errors cascade from a single root cause. Fixing one root issue can eliminate 10-50 errors at once.

5. **Scanner has no syntactic context.** The `|` suppression is a blunt instrument. A context-aware approach (e.g., tracking whether we're inside a match via the external scanner state) might help but adds significant complexity.

6. **`_expression` is the gateway to explosion.** Any rule that feeds back into `_expression` (directly or through `_do_seq → _do_element → _do_expression`) creates massive ambiguity. Keep new do-block constructs away from `_expression` where possible.

7. **Prec/prec.dynamic don't help with shift-reduce in GLR.** Tree-sitter often resolves conflicts deterministically without engaging GLR, ignoring dynamic precedence. Explicit `conflicts:` declarations are necessary but not sufficient.

8. **Small changes, measure immediately.** A single keyword addition can change state counts by thousands. Always generate and check after each change.

## Files Modified (vs. main branch)

```
grammar.js                     — externals, _do_seq, do, conflicts, let_mut prec, ∈ operators
grammar/command.js             — lemma, nonrec, opaque_decl, set_option
grammar/do.js                  — do_let rewrite, _do_element
grammar/tactic.js              — simp_all, split_ifs, omega, decide, assumption, etc.
grammar/term.js                — _where_decls indent, structure_instance indent
src/scanner.c                  — Full rewrite (Python-style whitespace consumption)
src/parser.c                   — Generated (do not edit)
src/grammar.json               — Generated (do not edit)
src/node-types.json            — Generated (do not edit)
test/corpus/do.txt             — Updated expected trees for do_let
```

## Build Commands

```sh
# Guarded generate (ALWAYS use this)
/usr/bin/time -v timeout 60s prlimit --as=8G tree-sitter generate --report-states-for-rule -

# Build shared library
tree-sitter build

# Run all tests
tree-sitter test

# Parse corpus files
./scripts/parse-dir.sh corpus

# Parse single file
tree-sitter parse path/to/file.lean
```
