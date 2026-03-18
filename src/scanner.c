#include <tree_sitter/parser.h>
#include <string.h>

#define MAX_DEPTH 100

enum TokenType {
  NEWLINE,
  INDENT,
  DEDENT,
  DO_OPEN,
  DO_SEPARATOR,
  DO_CLOSE,
};

typedef enum {
  CONTEXT_GENERIC,
  CONTEXT_DO,
} ContextType;

typedef struct {
  uint16_t column;
  uint8_t context;
} IndentEntry;

typedef struct {
  IndentEntry indent_stack[MAX_DEPTH];
  uint8_t depth;
  uint8_t pending_dedents;
  bool pending_newline;
  uint16_t target_indent;  // the indent level we're dedenting to
} Scanner;

// Get the current indent level from the stack top
static uint16_t current_indent(Scanner *scanner) {
  return scanner->depth > 0 ? scanner->indent_stack[scanner->depth - 1].column : 0;
}

// Get the current context from the stack top
static uint8_t current_context(Scanner *scanner) {
  return scanner->depth > 0 ? scanner->indent_stack[scanner->depth - 1].context : CONTEXT_GENERIC;
}

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  // Emit pending dedents one at a time.
  // Each call pops one entry from the stack and emits its close/dedent token.
  if (scanner->pending_dedents > 0) {
    // The stack top is the entry to pop — get its context
    uint8_t ctx = current_context(scanner);
    if (scanner->depth > 0) {
      scanner->depth--;
    }
    scanner->pending_dedents--;

    // After last dedent, check if we need a separator at the new level
    if (scanner->pending_dedents == 0) {
      uint16_t new_top = current_indent(scanner);
      if (scanner->target_indent == new_top) {
        scanner->pending_newline = true;
      }
    }

    if (ctx == CONTEXT_DO && valid_symbols[DO_CLOSE]) {
      lexer->result_symbol = DO_CLOSE;
      return true;
    }
    if (valid_symbols[DEDENT]) {
      lexer->result_symbol = DEDENT;
      return true;
    }
  }

  // After all dedents emitted, emit a pending newline/separator
  if (scanner->pending_newline) {
    uint8_t ctx = current_context(scanner);
    if (ctx == CONTEXT_DO && valid_symbols[DO_SEPARATOR] && lexer->lookahead != '|') {
      scanner->pending_newline = false;
      lexer->result_symbol = DO_SEPARATOR;
      return true;
    }
    if (valid_symbols[NEWLINE]) {
      scanner->pending_newline = false;
      // Suppress when next char is '|' to avoid splitting match expressions.
      if (lexer->lookahead != '|') {
        lexer->result_symbol = NEWLINE;
        return true;
      }
    }
  }
  scanner->pending_newline = false;

  // Need at least one external token to be valid
  if (!valid_symbols[NEWLINE] && !valid_symbols[INDENT] && !valid_symbols[DEDENT] &&
      !valid_symbols[DO_OPEN] && !valid_symbols[DO_SEPARATOR] && !valid_symbols[DO_CLOSE]) {
    return false;
  }

  // Mark the start for zero-width tokens
  lexer->mark_end(lexer);

  // Consume whitespace (including newlines) ourselves
  bool found_newline = false;
  uint32_t indent = 0;

  for (;;) {
    if (lexer->lookahead == '\n') {
      found_newline = true;
      indent = 0;
      lexer->advance(lexer, true);
    } else if (lexer->lookahead == '\r') {
      indent = 0;
      lexer->advance(lexer, true);
    } else if (lexer->lookahead == ' ') {
      indent++;
      lexer->advance(lexer, true);
    } else if (lexer->lookahead == '\t') {
      indent += 4;
      lexer->advance(lexer, true);
    } else if (lexer->eof(lexer)) {
      found_newline = true;
      indent = 0;
      break;
    } else {
      break;
    }
  }

  if (!found_newline) {
    return false;
  }

  // Re-measure: use get_column for the position of the first non-ws character
  indent = lexer->get_column(lexer);
  lexer->mark_end(lexer);

  uint16_t current = current_indent(scanner);
  uint8_t ctx = current_context(scanner);

  // --- Indent increased ---
  if (indent > current) {
    // DO_OPEN: grammar requests this only in do-block positions
    if (valid_symbols[DO_OPEN]) {
      if (scanner->depth < MAX_DEPTH) {
        scanner->indent_stack[scanner->depth].column = (uint16_t)indent;
        scanner->indent_stack[scanner->depth].context = CONTEXT_DO;
        scanner->depth++;
      }
      lexer->result_symbol = DO_OPEN;
      return true;
    }
    if (valid_symbols[INDENT]) {
      if (scanner->depth < MAX_DEPTH) {
        scanner->indent_stack[scanner->depth].column = (uint16_t)indent;
        scanner->indent_stack[scanner->depth].context = CONTEXT_GENERIC;
        scanner->depth++;
      }
      lexer->result_symbol = INDENT;
      return true;
    }
    // Indent increased but neither DO_OPEN nor INDENT expected.
    // Do NOT emit DO_SEPARATOR here — increased indent means continuation,
    // not a new do-element boundary (DO_SEPARATOR is for colEq only).
    if (valid_symbols[NEWLINE] && lexer->lookahead != '|') {
      lexer->result_symbol = NEWLINE;
      return true;
    }
    return false;
  }

  // --- Same indent ---
  if (indent == current) {
    if (ctx == CONTEXT_DO && valid_symbols[DO_SEPARATOR] && lexer->lookahead != '|') {
      lexer->result_symbol = DO_SEPARATOR;
      return true;
    }
    // Suppress NEWLINE when the next content starts with '|'
    if (valid_symbols[NEWLINE] && lexer->lookahead != '|') {
      lexer->result_symbol = NEWLINE;
      return true;
    }
    return false;
  }

  // --- Indent decreased ---
  // Count how many entries need to be popped (but don't pop yet — pending_dedents handles it)
  uint8_t dedents = 0;
  uint8_t d = scanner->depth;
  while (d > 0 && scanner->indent_stack[d - 1].column > indent) {
    d--;
    dedents++;
  }

  if (dedents > 0) {
    scanner->target_indent = (uint16_t)indent;

    // Pop the first entry now
    uint8_t first_ctx = current_context(scanner);
    scanner->depth--;
    scanner->pending_dedents = dedents - 1;

    // If no more dedents pending, check if we need a separator at the new level
    if (scanner->pending_dedents == 0) {
      uint16_t new_top = current_indent(scanner);
      if (scanner->target_indent == new_top) {
        scanner->pending_newline = true;
      }
    }

    // Emit first dedent based on context
    if (first_ctx == CONTEXT_DO && valid_symbols[DO_CLOSE]) {
      lexer->result_symbol = DO_CLOSE;
      return true;
    }
    if (valid_symbols[DEDENT]) {
      lexer->result_symbol = DEDENT;
      return true;
    }
  }

  // Fallback: emit separator if valid
  if (ctx == CONTEXT_DO && valid_symbols[DO_SEPARATOR]) {
    lexer->result_symbol = DO_SEPARATOR;
    return true;
  }
  if (valid_symbols[NEWLINE]) {
    lexer->result_symbol = NEWLINE;
    return true;
  }

  return false;
}

void *tree_sitter_lean_external_scanner_create() {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  scanner->indent_stack[0].column = 0;
  scanner->indent_stack[0].context = CONTEXT_GENERIC;
  scanner->depth = 1;
  scanner->pending_newline = false;
  scanner->target_indent = 0;
  return scanner;
}

bool tree_sitter_lean_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  return scan(scanner, lexer, valid_symbols);
}

unsigned tree_sitter_lean_external_scanner_serialize(void *payload,
                                                     char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  unsigned size = 0;

  buffer[size++] = scanner->depth;
  buffer[size++] = scanner->pending_dedents;
  buffer[size++] = scanner->pending_newline ? 1 : 0;
  buffer[size++] = (scanner->target_indent >> 8) & 0xFF;
  buffer[size++] = scanner->target_indent & 0xFF;

  // Serialize indent stack: 3 bytes per entry (column high, column low, context)
  for (uint8_t i = 0; i < scanner->depth && size + 3 <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE; i++) {
    buffer[size++] = (scanner->indent_stack[i].column >> 8) & 0xFF;
    buffer[size++] = scanner->indent_stack[i].column & 0xFF;
    buffer[size++] = scanner->indent_stack[i].context;
  }

  return size;
}

void tree_sitter_lean_external_scanner_deserialize(void *payload,
                                                   const char *buffer,
                                                   unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  scanner->depth = 1;
  scanner->pending_dedents = 0;
  scanner->pending_newline = false;
  scanner->target_indent = 0;
  scanner->indent_stack[0].column = 0;
  scanner->indent_stack[0].context = CONTEXT_GENERIC;

  if (length == 0) return;

  unsigned pos = 0;
  scanner->depth = buffer[pos++];
  if (pos >= length) return;
  scanner->pending_dedents = buffer[pos++];
  if (pos >= length) return;
  scanner->pending_newline = buffer[pos++] != 0;
  if (pos >= length) return;
  scanner->target_indent = ((uint8_t)buffer[pos] << 8) | (uint8_t)buffer[pos + 1];
  pos += 2;

  for (uint8_t i = 0; i < scanner->depth && pos + 2 < length; i++) {
    scanner->indent_stack[i].column = ((uint8_t)buffer[pos] << 8) | (uint8_t)buffer[pos + 1];
    scanner->indent_stack[i].context = (uint8_t)buffer[pos + 2];
    pos += 3;
  }
}

void tree_sitter_lean_external_scanner_destroy(void *payload) {
  free(payload);
}
