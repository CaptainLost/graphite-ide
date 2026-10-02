# 0434. NL.20: Don't place two statements on the same line

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Readability, and one specific failure of it: when a line holds more than one
statement, it is really easy to overlook the ones that are not first.

```cpp
int x = 7; char* p = 29;    // don't
int x = 7; f(x);  ++x;      // don't
```

The eye tracks down the left margin, so anything to the right of the first
statement on a line is read only by someone reading deliberately. The second
line is the sharper example: `++x` is a modification of a variable that the
reader has already seen initialised and used on the same line, and it is the
easiest of the three to miss.

The rule sits in a section the guidelines present as suggestions — layout
defaults for when there are no better ideas — but this one is among the more
mechanical of them, and upstream rates its enforcement as easy.

## Decision

Give every statement its own line.

- Do not separate two statements with `;` on one line, whether they are
  declarations, calls, or assignments.
- Put the body of an `if`, an `else`, or a loop on a line below the construct
  that introduces it, rather than trailing it on the same line.
- Do not compress a short sequence of related operations onto one line because
  they belong together; group them with a blank line above and below instead.
- Where a construct genuinely reads as a single statement — a chained call, a
  conditional expression — keeping it on one line is not what this rule
  forbids.

## Rationale

The argument is about scanning rather than about parsing. Code is skimmed far
more often than it is read closely, and a skim is a scan of the first statement
on each line; a statement placed second is therefore invisible in exactly the
mode in which most reading happens. The rule also makes line-oriented tools
honest — a diff, a blame, and a debugger breakpoint all address lines, and each
of them becomes ambiguous the moment a line holds two things.

## Consequences

### Positive

- Every statement is visible to a scan of the left margin, which is how code is
  usually read.
- Line-addressed tooling becomes precise: a diff shows which statement changed,
  and a breakpoint stops at one statement rather than three.
- Deleting or reordering a statement is a whole-line edit, so it is harder to
  leave a fragment behind.

### Negative

- It costs vertical space, which works against the compactness the surrounding
  layout rules are trying to preserve; a function that fitted on a screen may
  not afterwards.
- Idioms that read naturally on one line — a guard and its return, a swap in
  three assignments — get spread out, and the fact that they belong together is
  no longer visible in the layout.
- "Statement" is not defined for the purpose of the rule. A `for` header
  contains several statements by the grammar, and a lambda body can contain
  many, so the rule has to be read as being about statements in a block.
- The gain is smallest where the statements are shortest, which is precisely
  where the temptation to combine them is strongest, so the rule is most
  irritating in the cases it matters least for.

### Neutral

- The rule is about placement only; whether the statements should exist and in
  what order is a different question.
- It is the same requirement the K&R-derived layout states as one of its notes,
  restated so it can be applied on its own.

## Enforcement

Easy: flag any line containing more than one statement.

## Related

- [0435](0435-nl-21-declare-one-name-only-per-declaration.md) — the same
  discipline applied within a single declaration statement.
- [0180](0180-es-10-declare-one-name-only-per-declaration.md) — the reasoning
  behind that restriction, which this rule complements.
- [0431](0431-nl-17-use-k-r-derived-layout.md) — the layout rule that carries
  this requirement as one of its notes.
- [0422](0422-nl-4-maintain-a-consistent-indentation-style.md) — indentation
  only helps if each line holds one thing to indent.
