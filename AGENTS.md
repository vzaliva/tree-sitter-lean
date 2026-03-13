# AGENTS.md

This file provides guidance to AI agents like Claude Code, OpenAI Codex or Cursor when working with code in this repository.

## Commands

```sh
# Generate the parser from grammar.js, then run all tests
tree-sitter generate && tree-sitter test

# Run a single test file (filter by name)
tree-sitter generate && tree-sitter test -f "test name pattern"

# Parse a single Lean file
tree-sitter parse path/to/file.lean

# Parse all .lean files in a directory (parallel, with summary)
./scripts/parse-dir.sh <directory>
./scripts/parse-dir.sh -j 4 <directory>   # limit to 4 parallel jobs
```

`tree-sitter` is installed as a system binary (available in `$PATH`). Always invoke it directly (e.g., `tree-sitter generate`), never via `npx`, `npm run`, or any other npm-based method.

`src/parser.c`, `src/grammar.json`, and `src/node-types.json` are generated files — always regenerate with `tree-sitter generate` after editing `grammar.js` or any `grammar/*.js` file.

**CLI tools**: You can use: fd (fdfind), ripgrep, jq, rename, shellcheck, sd, delta, gh, difft (difftastic), scc, yq, hyperfine, watchexec, comby, tree-sitter, ast-grep, dtrx; fzf, bat, just, entr, rga (ripgrep-all), parallel (gnu parallel). For Lean workflows: fzf+bat (file/symbol nav + preview), just (task runner), entr or watchexec (rebuild on change), rga (search PDFs/docs/specs), parallel (safe parallel checks across files).

## Architecture

The grammar is split across several JavaScript modules that are merged into the top-level `grammar()` call in `grammar.js`:

- **`grammar.js`** — Entry point. Defines the top-level `module` rule, operator precedence table (`PREC`), shared expression rules (`_expression`, `binary_expression`, `fun`, `apply`, `let`, `do`, etc.), and merges all sub-modules via spread (`...attr`, `...command`, `...term.rules`, etc.).
- **`grammar/term.js`** — Term-level constructs (literals, binders, `match`, `have`, `forall`, `∃`, arrays, string interpolation). Exports a `Parser` instance (`term`) used with `term.all($)` or `term.forbid($, 'match')` to build filtered `choice()` sets without named rules.
- **`grammar/command.js`** — Top-level commands: `def`, `theorem`, `inductive`, `structure`, `class`, `namespace`, `section`, `open`, `variable`, `#check`, `#eval`, etc. Defines `_command`.
- **`grammar/do.js`** — `do`-notation: `do_let`, `do_let_arrow`, `do_if`, `do_for`, `do_return`, `do_match`, `do_try`. Exports both top-level properties and a `rules` object (the `rules` key is what gets spread into `grammar.js`).
- **`grammar/tactic.js`** — Tactic block (`by ...`): `apply`, `rewrite`/`rw`, `simp`, `intro`, `rfl`, `trivial`, and a catch-all `_user_tactic`.
- **`grammar/syntax.js`** — Lean meta-programming: `notation`, `mixfix`, `macro_rules`, `macro`, `elab`, `syntax`.
- **`grammar/attr.js`** — Attribute syntax (`@[...]`).
- **`grammar/basic.js`** — Shared constants (`PREC` with `max`/`arg`/`lead`/`min` levels) and `quoted_char`.
- **`grammar/util.js`** — Combinators: `sep0`, `sep1`, `sep1_` (trailing separator), `sep2`, `min1` (at-least-one of either of two things), and the `Parser` class.
- **`src/scanner.c`** — External scanner; currently only handles `_newline` (consumed as whitespace to separate tactic/do sequences).

### Key design patterns

- **`Parser` class** (`grammar/util.js`): wraps a list of term alternatives so they can be selectively excluded (e.g., `term.forbid($, 'match')` gives `_expression_no_fun` semantics without duplicating rules).
- **Conflict resolution**: many conflicts are declared explicitly in `grammar.js`'s `conflicts:` array rather than resolved by precedence alone; GLR disambiguation is common.
- **`_newline` external token**: used as a separator in `by`-blocks and `do`-blocks to express indentation-sensitivity without a full indent/dedent scanner.


### Building grammar

When working with a Tree-sitter grammar, **never run `tree-sitter build` directly**.
Always run `tree-sitter generate` first with **resource limits** to prevent state-space explosions from exhausting memory.

Use a guarded command:

```bash
/usr/bin/time -v timeout 60s prlimit --as=8G tree-sitter generate --report-states-for-rule -
```

Explanation:

* `timeout 60s` stops pathological runs.
* `prlimit --as=8G` caps the process virtual memory to ~8 GB.
* `/usr/bin/time -v` reports peak memory usage (Maximum resident set size).
* `--report-states-for-rule -` prints rule state counts to help detect explosions.

Only if generation succeeds should you run:

```bash
tree-sitter build
```

If generation **times out, exceeds memory, or becomes very slow**, stop immediately and diagnose the grammar rather than retrying blindly.

Common causes of parser generation blow-ups:

* ambiguous recursive rules (especially expression grammars)
* missing precedence (`prec`, `prec.left`, `prec.right`)
* very large `choice(...)` sets
* excessive or unnecessary `conflicts`
* overly broad recursive constructs

Recommended workflow:

1. Make **small grammar changes**.
2. Run guarded `tree-sitter generate`.
3. Inspect state reports if something looks suspicious.
4. Only then run `tree-sitter build`.

Prefer **refactoring problematic rules** over increasing resource limits.


### Tests

Tests live in `test/corpus/*.txt` using tree-sitter's corpus format:

```
===
Test name
===
lean source code
---
(expected_tree ...)
```

Files are organized by feature: `declarations.txt`, `expressions.txt`, `do.txt`, `notation.txt`, `organization.txt`, `commands.txt`, `tactics.txt`, `syntax.txt`, `strings.txt`, `comments.txt`.
