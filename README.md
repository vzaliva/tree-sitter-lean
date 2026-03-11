# tree-sitter-lean

Experimental start on a [Lean 4](https://leanprover.github.io/lean4/doc/) grammar for [tree-sitter](https://github.com/tree-sitter/tree-sitter).

For of https://github.com/Julian/tree-sitter-lean

[![Build status](https://github.com/Julian/tree-sitter-lean/workflows/CI/badge.svg)](https://github.com/Julian/tree-sitter-lean/actions?query=workflow%3ACI)

Can be used standalone, or in neovim with
[nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter) via
[lean.nvim](https://github.com/Julian/lean.nvim).

## Testing

Tests for the grammar live in `test/corpus/*.txt`, and can be run as usual for
`tree-sitter` grammars with the
[tree-sitter-cli](https://tree-sitter.github.io/tree-sitter/creating-parsers#command-test)
via:

```sh
$ tree-sitter generate && tree-sitter test
```
