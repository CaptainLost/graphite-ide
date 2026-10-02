# 0421. NL.3: Keep comments crisp

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Verbosity slows down understanding, and it makes the code harder to read by
spreading it around in the source file. A paragraph of commentary pushes the
code it describes off the screen, so the reader pays for the prose twice: once
to read it, and again in the context they lost while reading.

Upstream adds a second requirement to the first. Use intelligible English: the
author may be fluent in Danish, but most programmers are not, and the
maintainers of the code might not be. Avoid SMS lingo, and watch grammar,
punctuation and capitalisation. Aim for professionalism, not for "cool".

Upstream states that enforcement is not possible.

## Decision

Keep comments short enough to be read and clear enough to be understood.

- Say the thing once, in as few sentences as carry it, and stop.
- Write in ordinary English prose with normal grammar, punctuation and
  capitalisation — not abbreviations, not chat shorthand, not jokes that need
  the author present to land.
- Prefer several short comments placed where they apply to one long block at the
  top of a file that no one reads.
- When a comment grows past a few lines because the code is hard to follow, fix
  the code or extract a named function instead of explaining harder.

## Rationale

A comment competes with the code for the reader's screen and attention, and it
always loses on value per line, because only the code is checked. The practical
test is whether the comment will still be read in a year: a long one will be
skipped, which makes it exactly as useful as no comment while still needing
maintenance.

## Consequences

### Positive

- Comments stay on screen with the code they describe, so both are read
  together.
- Short comments are cheap to keep true, which is the main reason comments go
  stale.
- Requiring plain English makes the code readable by maintainers who do not
  share the author's language or in-jokes.

### Negative

- Brevity and completeness genuinely conflict. Some contracts — an ordering
  guarantee, an aliasing rule, a threading requirement — cannot be stated
  crisply, and trimming them loses information a caller needs.
- "Crisp" is unmeasurable, so the rule cannot settle an argument; it can only
  be invoked in one.
- It pulls against [0420](0420-nl-2-state-intent-in-comments.md) in practice.
  Stating intent thoroughly and keeping the comment short are the same sentence
  pulling in two directions, and the balance is the author's judgement each
  time.

### Neutral

- The rule is about length and register, not about how many comments to write.
- Longer explanations are not banned outright; they belong somewhere a reader
  can choose to go, such as a record in this directory, rather than between two
  statements.

## Enforcement

Upstream states plainly that enforcement is not possible. Nothing can judge
whether a comment is crisp, so this is a review rule.

## Related

- [0420](0420-nl-2-state-intent-in-comments.md) — what the surviving comments
  should contain, and the rule this one tempers.
- [0419](0419-nl-1-dont-say-in-comments-what-can-be-clearly-stated-in-code.md) —
  the cheapest way to shorten comments is to delete the redundant ones.
- [0433](0433-nl-19-avoid-names-that-are-easily-misread.md) — the same concern
  for the reader, applied to identifiers rather than prose.
