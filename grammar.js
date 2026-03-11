const attr = require('./grammar/attr.js')
const basic = require('./grammar/basic.js')
const command = require('./grammar/command.js')
const do_ = require('./grammar/do.js')
const syntax = require('./grammar/syntax.js')
const tactic = require('./grammar/tactic.js')
const term = require('./grammar/term.js')
const {sep1} = require('./grammar/util.js')

const PREC = {
  dollar: -5,
  equal: -3,
  compare: -2,
  apply: -1,
  multitype: -1,

  opop: 13,
  or: 14,
  and: 15,
  eqeq: 16,
  plus: 17,
  times: 18,
  unary: 1000,
  power: 20,

  name: 30,
}

module.exports = grammar({
  name: 'lean',

  extras: $ => [
    $.comment,
    /\s/,
  ],

  externals: $ => [
    $._newline,
    $._indent,
    $._dedent,
  ],

  conflicts: $ => [
    [$._binder_ident, $._term],
    [$._binder_ident, $.named_argument],
    [$._binder_ident, $.subtype],
    [$._binder_ident],
    [$._have_id_decl, $._expression],
    [$._have_id_lhs, $._term],
    [$._have_id_lhs],
    [$._let_id_lhs, $._term],
    [$._let_id_lhs],
    [$._simple_binder],
    [$._user_tactic, $.quoted_tactic],
    [$.assign, $._term],
    [$.identifier],
    [$.instance_binder, $._term],
    [$.instance_binder, $.list],
    [$.proj, $._expression],
    [$.cdot, $.dot_identifier],
    [$.do_match],
    [$._expression, $._do_element],
    [$.do_let, $.let],
    [$.do_let, $.let_rec],
    [$.do_let, $.let_mut],
    [$.do_let, $.parameters],
  ],

  word: $ => $._identifier,

  rules: {
    // src/Lean/Parser/Module.lean
    module: $ => seq(
      optional($.prelude),
      repeat($.import),
      repeat($._command),
    ),
    prelude: $ => 'prelude',
    import: $ => seq('import', field('module', $.identifier)),

    parameters: $ => seq(
      repeat1(
        choice(
          field('name', $.identifier),
          $.hole,
          $._bracketed_binder,
          $.anonymous_constructor,
        )
      ),
    ),

    _expression: $ => choice(
      $.apply,
      $.comparison,
      $.let_rec,
      $.let,
      $.tactics,
      $.binary_expression,
      $.neg,
      $.quoted_tactic,
      $.fun,
      $._term,

      $.do,
      $.unless,
      $.do_return,
    ),

    // Match pattern lhs: expression without fun/lambda (prevents "v1 =>" being parsed as lambda)
    _expression_no_fun: $ => choice(
      $.apply,
      $.comparison,
      $.let_rec,
      $.let,
      $.tactics,
      $.binary_expression,
      $.neg,
      $.quoted_tactic,
      $._term,
      $.do,
      $.unless,
    ),

    // let rec with equations: let rec f : Type | pat => body | pat => body
    let_rec: $ => prec.left(1, seq(
      'let',
      'rec',
      field('name', $.identifier),
      optional(field('parameters', $.parameters)),
      optional(seq(':', field('type', $._expression))),
      optional(choice($._newline, ';')),
      field('value', $._match_alts),
      optional(field('body', $._expression)),
    )),

    let: $ => prec.left(seq(
      'let',
      field('name', $.identifier),
      optional(field('parameters', $.parameters)),
      optional(seq(':', field('type', $._expression))),
      ':=',
      field('value', $._expression),
      choice($._newline, ';'),
      optional(field('body', $._expression)),
    )),

    _do_seq: $ => choice(
      seq($._indent, sep1($._do_element, $._newline), $._dedent),
      $._do_element,
    ),
    do: $ => prec.right(seq('do', $._do_seq)),

    for_in: $ => seq(
      'for',
      choice($.identifier, $.anonymous_constructor),
      'in',
      field('iterable', $._expression),
      field('body', $.do),
    ),

    assign: $ => seq(
      field('name', $.identifier),
      ':=',
      field('value', $._expression),
    ),

    let_mut: $ => prec(100, seq(
      'let', 'mut',
      $.parameters,
      choice($._left_arrow, ':='),
      field('value', $._expression),
    )),

    let_bind: $ => seq(
      'let',
      field('name', choice($.identifier, $.anonymous_constructor, $.parenthesized)),
      $._left_arrow,
      field('value', $._expression),
    ),

    unless: $ => seq('unless', $._expression, $.do),

    try: $ => prec.left(1, seq(
      'try',
      $._do_seq,
      choice(
        seq($.catch, optional($.finally)),
        $.finally,
    ))),

    catch: $ => prec.left(seq(
      'catch',
      $._expression,
      '=>',
      $._do_seq,
    )),

    finally: $ => prec.left(seq(
      'finally',
      $._do_seq,
    )),

    fun: $ => prec.right(seq(
      choice('fun', 'λ'),
      choice(
        seq(
          $.parameters,
          '=>',
          $._expression,
        ),
        repeat1(seq(
          '|',
          field('lhs', sep1($._expression, ',')),
          '=>',
          $._expression,
        )),
      ),
    )),

    apply: $ => choice($._apply, $._dollar),

    _apply: $ => prec(PREC.apply, seq(
      field('name', term.term.forbid($, 'match')),
      field('arguments', repeat1($._argument)),
    )),

    // FIXME: This is almost certainly wrong
    _dollar: $ => prec.right(PREC.dollar, seq(
      field('name', $._expression),
      '$',
      field('argument', $._expression),
    )),

    neg: $ => prec(PREC.unary, seq('-', $._expression)),

    binary_expression: $ => choice(
      prec.right(PREC.power, seq($._expression, '^', $._expression)),
      prec.left(PREC.times, seq($._expression, '*', $._expression)),
      prec.left(PREC.times, seq($._expression, '/', $._expression)),
      prec.left(PREC.times, seq($._expression, '%', $._expression)),
      prec.left(PREC.plus, seq($._expression, '+', $._expression)),
      prec.left(PREC.plus, seq($._expression, '-', $._expression)),

      prec.right(PREC.plus, seq($._expression, '∘', $._expression)),

      prec.left(PREC.opop, seq($._expression, '∧', $._expression)),
      prec.left(PREC.opop, seq($._expression, '∨', $._expression)),
      prec.left(PREC.opop, seq($._expression, '/\\', $._expression)),
      prec.left(PREC.opop, seq($._expression, '\\/', $._expression)),
      prec.left(PREC.opop, seq($._expression, '↔', $._expression)),

      prec.left(PREC.or, seq($._expression, '||', $._expression)),
      prec.left(PREC.and, seq($._expression, '&&', $._expression)),
      prec.left(PREC.eqeq, seq($._expression, '==', $._expression)),

      prec.left(PREC.opop, seq($._expression, '++', $._expression)),
      prec.left(PREC.opop, seq($._expression, '::', $._expression)),

      prec.left(PREC.opop, seq($._expression, '|>', $._expression)),
      prec.left(PREC.opop, seq($._expression, '|>.', $._expression)),
      prec.right(PREC.dollar, seq($._expression, '<|', $._expression)),

      prec.left(PREC.opop, seq($._expression, '<|>', $._expression)),
      prec.left(PREC.opop, seq($._expression, '>>', $._expression)),
      prec.left(PREC.opop, seq($._expression, '>>=', $._expression)),
      prec.left(PREC.opop, seq($._expression, '<*>', $._expression)),
      prec.left(PREC.opop, seq($._expression, '<*', $._expression)),
      prec.left(PREC.opop, seq($._expression, '*>', $._expression)),
      prec.left(PREC.opop, seq($._expression, '<$>', $._expression)),

      prec.left(PREC.equal, seq($._expression, '=', $._expression)),
      prec.left(PREC.equal, seq($._expression, '≠', $._expression)),
      prec.left(PREC.equal, seq($._expression, '!=', $._expression)),
      prec.left(PREC.equal, seq($._expression, '!=', $._expression)),

    ),

    comparison: $ => prec.left(PREC.compare, seq(
      $._expression,
      choice(
        '<',
        '>',
        '≤',
        '≥',
        '<=',
        '>=',
        '!=',
        '∈',
        '∉',
        '⊆',
        '⊂',
        '∩',
        '∪',
      ),
      $._expression,
    )),

    comment: $ => token(choice(
      seq('--', /.*/),
      /\/-([^-]|-+[^-/])*-+\//,
    )),

    _identifier: $ => /[_a-zA-ZͰ-ϿĀ-ſ\U0001D400-\U0001D7FF][_`'`a-zA-Z0-9Ͱ-ϿĀ-ſ∇!?\u2070-\u209F\U0001D400-\U0001D7FF]*/,
    _escaped_identifier: $ =>  /«[^»]*»/,

    ...attr,
    ...command,
    ...syntax,
    ...tactic,
    ...basic.rules,
    ...do_.rules,
    ...term.rules,
  }
});
