#include <tree_sitter/parser.h>
#include <string.h>

#define MAX_DEPTH 100

enum TokenType {
  NEWLINE,
  INDENT,
  DEDENT,
};

typedef struct {
  uint16_t indent_stack[MAX_DEPTH];
  uint8_t depth;
  uint8_t pending_dedents;
  bool pending_newline;
} Scanner;

static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  // Emit pending dedents first
  if (scanner->pending_dedents > 0 && valid_symbols[DEDENT]) {
    scanner->pending_dedents--;
    lexer->result_symbol = DEDENT;
    return true;
  }

  // After all dedents emitted, emit a pending newline if the new indent
  // matches the current stack level (acts as separator at outer block level).
  // Suppress when next char is '|' to avoid splitting match expressions.
  if (scanner->pending_newline && valid_symbols[NEWLINE]) {
    scanner->pending_newline = false;
    if (lexer->lookahead != '|') {
      lexer->result_symbol = NEWLINE;
      return true;
    }
  }
  scanner->pending_newline = false;

  // Need at least one external token to be valid
  if (!valid_symbols[NEWLINE] && !valid_symbols[INDENT] && !valid_symbols[DEDENT]) {
    return false;
  }

  // Mark the start for zero-width tokens
  lexer->mark_end(lexer);

  // Consume whitespace (including newlines) ourselves, like Python's scanner.
  // This is necessary because tree-sitter extras (/\s/) may or may not have
  // consumed newlines before calling the scanner.
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

  // Skip comments on otherwise blank lines (-- comment\n pattern)
  // and blank lines between content
  // Re-measure: use get_column for the position of the first non-ws character
  indent = lexer->get_column(lexer);

  lexer->mark_end(lexer);

  uint16_t current = scanner->depth > 0 ? scanner->indent_stack[scanner->depth - 1] : 0;

  if (indent > current) {
    if (valid_symbols[INDENT]) {
      if (scanner->depth < MAX_DEPTH) {
        scanner->indent_stack[scanner->depth++] = (uint16_t)indent;
      }
      lexer->result_symbol = INDENT;
      return true;
    }
    // Indent increased but not expected — emit NEWLINE as fallback.
    // Suppress when next char is '|' to avoid splitting match expressions.
    if (valid_symbols[NEWLINE] && lexer->lookahead != '|') {
      lexer->result_symbol = NEWLINE;
      return true;
    }
    return false;
  }

  if (indent == current) {
    // Suppress NEWLINE when the next content starts with '|' — this is likely
    // a match arm continuation and not a new do-element separator.
    if (valid_symbols[NEWLINE] && lexer->lookahead != '|') {
      lexer->result_symbol = NEWLINE;
      return true;
    }
    return false;
  }

  // indent < current
  if (valid_symbols[DEDENT]) {
    uint8_t dedents = 0;
    while (scanner->depth > 0 && scanner->indent_stack[scanner->depth - 1] > indent) {
      scanner->depth--;
      dedents++;
    }
    if (dedents > 0) {
      scanner->pending_dedents = dedents - 1;
      // After all dedents, if the indent matches the new stack top,
      // we need a NEWLINE to act as separator at the outer level
      uint16_t new_top = scanner->depth > 0 ? scanner->indent_stack[scanner->depth - 1] : 0;
      if (indent == new_top) {
        scanner->pending_newline = true;
      }
      lexer->result_symbol = DEDENT;
      return true;
    }
  }

  // Fallback: emit NEWLINE if valid
  if (valid_symbols[NEWLINE]) {
    lexer->result_symbol = NEWLINE;
    return true;
  }

  return false;
}

void *tree_sitter_lean_external_scanner_create() {
  Scanner *scanner = calloc(1, sizeof(Scanner));
  scanner->indent_stack[0] = 0;
  scanner->depth = 1;
  scanner->pending_newline = false;
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

  // Serialize depth
  buffer[size++] = scanner->depth;
  // Serialize pending_dedents
  buffer[size++] = scanner->pending_dedents;
  // Serialize pending_newline
  buffer[size++] = scanner->pending_newline ? 1 : 0;
  // Serialize indent stack
  for (uint8_t i = 0; i < scanner->depth && size + 2 <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE; i++) {
    buffer[size++] = (scanner->indent_stack[i] >> 8) & 0xFF;
    buffer[size++] = scanner->indent_stack[i] & 0xFF;
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
  scanner->indent_stack[0] = 0;

  if (length == 0) return;

  unsigned pos = 0;
  scanner->depth = buffer[pos++];
  if (pos >= length) return;
  scanner->pending_dedents = buffer[pos++];
  if (pos >= length) return;
  scanner->pending_newline = buffer[pos++] != 0;

  for (uint8_t i = 0; i < scanner->depth && pos + 1 < length; i++) {
    scanner->indent_stack[i] = ((uint8_t)buffer[pos] << 8) | (uint8_t)buffer[pos + 1];
    pos += 2;
  }
}

void tree_sitter_lean_external_scanner_destroy(void *payload) {
  free(payload);
}
