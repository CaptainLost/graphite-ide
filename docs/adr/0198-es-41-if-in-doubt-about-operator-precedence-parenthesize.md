# 0198. ES.41: If in doubt about operator precedence, parenthesize

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Parentheses avoid errors and improve readability. Not everyone has the operator
table memorised, and the corners of that table are where the bugs are:

```cpp
const unsigned int flag = 2;
unsigned int a = flag;

if (a & flag != 0)  // bad: means a&(flag != 0)
```

The comparison binds tighter than the bitwise `and`, so this tests `a & 1`,
which is not what the author meant. Written out, it works as intended:

```cpp
if ((a & flag) != 0)  // OK: works as intended
```

The guidelines recommend that programmers know the precedence table for the
arithmetic and logical operations, but treat any mixture of the bitwise logical
operators with other operators as in need of parentheses.

The rule is bounded on the other side too. You should know enough not to need
parentheses for an expression like this one:

```cpp
if (a < 0 || a <= max) {
    // ...
}
```

## Decision

Parenthesize where the precedence is not common knowledge, and leave it alone
where it is.

- Always parenthesize the operands when a bitwise operator (`&`, `|`, `^`,
  `<<`, `>>`) appears in the same expression as a comparison, a logical
  operator, or arithmetic.
- Do not write an assignment as anything other than the leftmost operator of an
  expression; an assignment buried in a subexpression is a violation of
  [0197](0197-es-40-avoid-complicated-expressions.md) as well.
- Do not add parentheses to `a * b + c`, `a < 0 || a <= max`, or other
  combinations from the arithmetic and logical groups that every C++ programmer
  reads correctly.
- If you had to look the precedence up in order to write the line, add the
  parentheses; the next reader will have to look it up too.

## Rationale

The failure mode this rule addresses is not that the compiler is confused — the
compiler is never confused — but that the wrong parse still compiles, still has
the right type, and still produces a plausible value. `a & flag != 0` yields a
`bool`-shaped truth that is wrong roughly half the time, and no diagnostic
fires.

Parenthesizing everything would be the safe extreme, and the rule explicitly
declines it: parentheses on expressions everyone already reads correctly are
noise, and noise is what hides the parentheses that matter.

## Consequences

### Positive

- The most common precedence trap in C and C++ — bitwise operators against
  comparisons — is closed by a rule that needs no judgement to apply.
- A reader can trust that the parentheses present are the ones that carry
  meaning, because gratuitous ones are also ruled out.
- The rule survives a change of operand types, which precedence-based reasoning
  does not: overloading `&` or `<<` for a user-defined type does not change its
  precedence, only its meaning.

### Negative

- "In doubt" is the author's doubt, not the reader's. The person most confident
  about the precedence table is exactly the person who will not add the
  parentheses that the next reader needed.
- Over-parenthesizing is a real cost that this rule tolerates rather than
  prevents. A fully bracketed expression is harder to scan than a correctly
  precedence-ordered one, because the structure is no longer conveyed by shape.
- Mechanical enforcement produces false positives on `1 << n` and similar
  shift-arithmetic idioms, where the mixture is intended and unambiguous.
- The rule says nothing about associativity, which has its own traps, and
  nothing about the precedence of the conditional operator, which is a
  frequent surprise.

### Neutral

- The rule governs where parentheses go, not which operators may be mixed; an
  expression that does not mix the groups needs no parentheses either way.
- Where a formatter or an analyser inserts the parentheses mechanically, the
  rule is discharged by the tool rather than by review.

## Enforcement

- Flag combinations of bitwise-logical operators with other operators.
- Flag assignment operators appearing anywhere but as the leftmost operator of
  an expression.

## Related

- [0197](0197-es-40-avoid-complicated-expressions.md) — an expression relying on
  subtle precedence is one of that rule's listed bad shapes.
- [0009](0009-p-3-express-intent.md) — parentheses that state the grouping state
  the intent.
- [0205](0205-es-48-avoid-casts.md) — the other common source of expressions
  that compile but mean something unintended.
