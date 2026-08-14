# 0484. Keep cross-line lexer state opaque

Status: proposed
Date: 2026-08-14

## Context

[0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) has every
lexer return a `LineState` that the line below it is entered in. That type sits
in `Lang/Syntax/Token.h`, shared by every language, and it originally held a
`Continuation` enum whose values were C++'s: `BlockComment`, `RawString`,
`LineComment`, `String`, `Character`.

`Lang/Api` — `LanguageId`, `TokenKind` — is genuinely language-neutral, and a
second lexer joins it as a table row, an enum value and a new class.
`Lang/Syntax` was not. Its vocabulary was one language's.

C# alone needs three cross-line states that vocabulary cannot express:

- a verbatim string `@"..."`, whose escape rule is a doubled quote rather than
  a backslash, so it cannot reuse the `String` case;
- a raw string literal, which resumes on a *count of quotes* rather than on a
  delimiter string;
- an interpolated string, which must carry a brace-nesting depth.

So adding a language meant editing the type every other language returns —
the one thing a shared vocabulary exists to prevent. The cost of leaving it
grows with every reader of `LineState`: at the time of this decision there were
two, `CppLexer` and `SyntaxCache`.

`SyntaxCache` does exactly two things with a `LineState`: it stores one per line
and it compares them. Nothing reads the meaning of one.

## Decision

`LineState` carries no language's vocabulary. It holds a `std::uint32_t Mode`,
opaque to everything but the lexer that produced it, and a `std::string
Delimiter` for a resume condition too wide to fit in the mode word.

Zero means "no continuation" in every language, so a default-constructed
`LineState` is the start of a file whichever lexer reads it. Every other value
belongs to its producer and means nothing to another lexer.

A lexer declares its own continuation enum in its `.cpp`, in an unnamed
namespace, and converts at exactly two points — one function building a
`LineState` from the enum, one reading it back. No other file names those
values.

Tests assert that a line carried *something* over, and prove *what* by the
shape of the line that resumes it. They do not name a lexer's internal state.

## Rationale

The shared type's real requirement is that it be storable and comparable, and
an opaque word satisfies that completely. Making the enum a union of every
language's constructs satisfies it too, but grows without bound and puts a
shared-header edit in the path of every new language — the exact cost this
record exists to remove.

## Consequences

### Positive

Adding a language touches no shared type. C#'s quote count and brace depth both
fit in the mode word, so the three states named above need no new field.

The tests stopped depending on the lexer's internal representation, so they now
fail for behavioural reasons rather than for renames.

### Negative

Nothing type-checks a mode word. A lexer that returns the wrong value produces
wrong colours, and a debugger shows a number where it used to show a name.
Keeping the enum in the lexer and casting only at the boundary limits the blast
radius to one file but does not remove the hazard.

A `LineState` can no longer be constructed meaningfully outside the lexer that
understands it — which is why the tests obtain one by lexing a line rather than
by naming a state.

### Neutral

`Delimiter` stays as it was. It is not C++-specific in shape, only in current
use.

## Alternatives Considered

**Let `Continuation` be the union of every language's cross-line constructs,
and say so here.** Rejected. It grows forever, and because this project builds
with `fatalwarnings`, adding an enumerator breaks the switch in every existing
lexer — forcing a review of files the change does not concern. That coupling is
what a shared vocabulary is supposed to avoid, not something to formalise.

**Make `LineState` a `std::variant` over each language's state type.** Rejected.
The shared header would then have to name every language's type, which is the
same coupling with more machinery, and `Lang/Syntax` would have to include
every lexer's header.

## Related

- [0481](0481-lex-one-line-at-a-time-carrying-a-resumable-state.md) — the
  line-at-a-time contract this refines
- [0482](0482-put-the-language-layer-in-graphite.md) — why the language layer
  is not in `Core`
