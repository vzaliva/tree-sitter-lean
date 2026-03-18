const {PREC} = require('./basic.js')
const {term} = require('./term.js')

// src/Lean/Parser/Do.lean
module.exports = {
  rules: {
    _left_arrow: $ => choice('<-', '←'),

    // return with optional value — only valid inside do blocks
    do_return: $ => prec.left(PREC.lead,
      seq('return', optional(field('value', prec(PREC.lead, $._expression)))),
    ),

    // match <- expr with | pat => body (do-notation monadic match)
    do_match: $ => prec.left(seq(
      'match',
      $._left_arrow,
      field('value', $._expression),
      'with',
      field('patterns', $._match_alts),
    )),

    // let name [:type] := value (do-notation let)
    do_let: $ => seq(
      'let',
      field('name', choice($.identifier, $.hole, $.parenthesized, $.anonymous_constructor)),
      optional(field('parameters', $.parameters)),
      optional(seq(':', field('type', $._expression))),
      ':=',
      field('value', $._expression),
    ),

    _do_element: $ => choice(
      $.do_match,
      $.do_let,
      $._expression,
      $.assign,
      $.for_in,
      $.let_bind,
      $.let_mut,
      $.do_return,
    ),
  },
}
