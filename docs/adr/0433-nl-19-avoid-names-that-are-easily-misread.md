# 0433. NL.19: Avoid names that are easily misread

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Readability is the whole of the reason. Not everyone has screens and printers
that make it easy to tell all characters apart, and readers confuse similarly
spelled and slightly misspelled words even when the glyphs are distinct.

```cpp
int oO01lL = 6; // bad
int splunk = 7;
int splonk = 8; // bad: splunk and splonk are easily confused
```

The first line fails at the level of the glyph: capital `O` against zero,
lower-case `l` against one, and each against its neighbours. The second failure
is at the level of the word. `splunk` and `splonk` are perfectly legible
individually; they are unsafe because they differ in one interior letter, so a
reader who has seen one will read the other as it without noticing, and a
mistyped use of either compiles.

This rule belongs to a section the guidelines describe as suggestions —
defaults offered for use when there are no better ideas, with consistency
rather than any particular rule as the real aim.

## Decision

Choose names that cannot be misread for one another.

- Avoid `l`, `I`, `O` and their look-alikes `1` and `0` as identifiers or as
  distinguishing characters within an identifier.
- Do not let two names in the same scope differ only by an interior letter, by
  a single transposition, or by case alone.
- Do not distinguish names by a trailing digit where the digit carries the
  entire meaning.
- When two things are similar enough that their names come out similar, name
  them after what distinguishes them rather than adding a suffix.
- Prefer a name that survives a bad font, a printout, or a diff read quickly.

## Rationale

The failure this rule prevents is silent. A misread name is not a compiler
error and usually not a test failure — it produces code that does the wrong
thing with the wrong variable and reads correctly to the person who wrote it
and to the reviewer who checks it. The cost of picking a different word is paid
once, at the moment of naming, by the only person who ever has the whole
context.

## Consequences

### Positive

- Confusable pairs cannot cause a review to pass over a substitution that a
  test may not catch either.
- Names stay legible outside the editor that was tuned to make them legible —
  in a terminal diff, a printout, or a screenshot.
- The discipline pushes towards names that describe the difference between two
  things, which is usually the more informative name anyway.

### Negative

- "Easily misread" has no definition and no threshold. Whether two names are
  too close is a judgement, and two reviewers can disagree without either being
  wrong.
- The rule depends on the reader's font and eyesight, neither of which the
  author controls, so it is being applied to a moving target.
- It pushes names longer at exactly the point where the naming rules ask for
  short names in short scopes, and the two give no guidance on which yields.
- Upstream supplies no enforcement, so nothing detects a confusable pair when
  one is introduced later by a name that was fine when it was added.

### Neutral

- The rule constrains spelling only; it says nothing about whether a name means
  the right thing.
- Conventional short names such as `i` and `p` are not the target here — the
  target is names that resemble each other or resemble digits.

## Related

- [0178](0178-es-8-avoid-similar-looking-names.md) — the same rule stated among
  the expression and statement guidelines, with its own examples.
- [0424](0424-nl-7-make-the-length-of-a-name-roughly-proportional-to-the-length-of-its-scope.md)
  — the pressure towards short names, which this rule pushes back against.
- [0425](0425-nl-8-use-a-consistent-naming-style.md) — a consistent style
  reduces the number of ways two names can differ accidentally.
- [0423](0423-nl-5-avoid-encoding-type-information-in-names.md) — encoded
  prefixes and suffixes are a common source of near-identical names.
