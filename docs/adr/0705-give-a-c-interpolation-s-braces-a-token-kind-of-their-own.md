# 0705. Give a C# interpolation's braces a token kind of their own

Status: proposed
Date: 2026-11-15

## Context

In `$"checking {number} - {IsPrimeNumber(number)}"`, the braces were captured as `@string`. That came from a fix
in which, captured as punctuation, they ended a definition's extent early: `DefinitionExtent` counts braces among
Punctuation tokens. In the Graphite and Gruvbox dark schemes String is green, so the braces stood out as green spots
between the coloured code. They looked like comment colour, and as part of the string they read as text, not code.

## Decision

- Add `TokenKind::Interpolation`, captured from `@punctuation.interpolation`, which the C# query gives
  `(interpolation_brace)`.
- Give it a colour of its own in every scheme: the built-in Dark and Light take Visual Studio's interpolation blue,
  and the derived schemes take their keyword colour. It is editable in Settings as "Interpolation braces".
- Append it after the semantic kinds, which analysis records store by number.

## Rationale

A kind of its own keeps the braces out of both things that went wrong: they are not Punctuation, so brace counting
still skips them, and not String, so they no longer take the string's colour.

## Consequences

### Positive

- The braces read as the start and end of code, in a colour that differs from the string around them.
- Matching-bracket highlighting now pairs an interpolation's braces, since they are no longer string text.

### Negative

- One more colour for each scheme to define. A user's own scheme that does not set it takes its base scheme's
  colour.

### Neutral

- C++ has no interpolated strings, so its lexer never produces the kind.

## Alternatives Considered

- **Capture them as Punctuation again and teach `DefinitionExtent` to skip braces inside strings.** One kind fewer,
  but the extent counter would need string spans it does not have today, and the fix it reverses would come back
  for every other reader of Punctuation.
- **Keep them String and change the scheme's string colour.** Hides the symptom in one scheme only.

## Related

- [0703](0703-help-typing-around-brackets-and-reindent-pasted-lines-from-the-text-alone.md)
