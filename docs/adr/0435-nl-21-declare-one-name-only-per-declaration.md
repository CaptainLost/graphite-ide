# 0435. NL.21: Declare one name (only) per declaration

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Upstream supplies almost nothing for this rule. It gives a two-word reason —
readability, and minimising confusion with the declarator syntax — and a note
directing the reader to ES.10 for the details. There is no example, no
exception and no enforcement clause, so there is nothing further to transcribe
here; the substance of the rule lives in ES.10, which is recorded separately.

## Decision

Declare one name per declaration.

- Give each declared object, pointer or reference its own declaration statement
  with its own type.
- Do not attach a second declarator to a declaration, in a block, in a class
  body, or at namespace scope.

## Rationale

The declarator syntax distributes differently from how it reads: the `*` or `&`
binds to the name, not to the type, so a second name on the same declaration
does not get the type the layout suggests. Declaring one name at a time removes
the question entirely.

## Consequences

### Positive

- A declaration cannot be misread, because there is only one name for the type
  to apply to.
- Each declaration can be initialised, commented, moved or deleted on its own.

### Negative

- The record rests on a title and a one-line reason, so it cannot be checked
  against upstream reasoning; anything beyond that restatement is inference.
- It costs a line per name, which works against keeping a group of closely
  related declarations visibly together.

### Neutral

- The rule is stated twice in the guidelines, here as a layout suggestion and
  in ES.10 as a substantive one; the two do not disagree.

## Related

- [0180](0180-es-10-declare-one-name-only-per-declaration.md) — the same rule
  with the reasoning and examples this one defers to.
- [0432](0432-nl-18-use-c-style-declarator-layout.md) — the spacing convention
  that makes multiple declarators actively misleading.
- [0434](0434-nl-20-dont-place-two-statements-on-the-same-line.md) — the
  neighbouring rule, applied to statements rather than to declarators.
