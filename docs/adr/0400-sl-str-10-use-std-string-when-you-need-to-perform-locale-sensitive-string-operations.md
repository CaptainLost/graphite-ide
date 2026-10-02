# 0400. SL.str.10: Use std::string when you need to perform locale-sensitive string operations

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Upstream supplies a title and a single-line reason — that `std::string` is
supported by the standard library's locale facilities — and then leaves the
example, the note and the enforcement as placeholders. There is nothing further
to transcribe.

## Decision

Where a string operation depends on a locale — case conversion, collation,
numeric or monetary formatting, or classification of characters beyond the basic
set — hold the text in `std::string` and use the standard `<locale>` facilities
on it, rather than performing the operation on a raw character buffer.

## Rationale

The locale facilities are defined in terms of the standard string and stream
types, so text held in anything else has to be converted before those facilities
can be applied at all.

## Consequences

### Positive

- Locale-dependent operations are performed by the facilities designed for them
  rather than by hand.
- The text is already in the type the rest of the standard library expects.

### Negative

- This record rests on a title and one sentence of upstream reasoning, with no
  example, note or enforcement, so it cannot be checked against any upstream
  argument beyond that sentence.
- It gives no guidance on the hard part of the subject, which is encoding:
  `std::string` is a sequence of `char`, and locale-sensitive operations on
  multi-byte encodings are not solved by choosing this type.
- The standard `<locale>` facilities are widely considered awkward and are
  avoided in practice, so the rule points at machinery a project may reasonably
  decline to use.

### Neutral

- The rule adds nothing to
  [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) except the
  observation that locale work is another reason to hold text in `std::string`.

## Related

- [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) — the
  general rule this is a special case of.
- [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md)
  — the non-owning counterpart, which the locale facilities do not accept as
  readily.
- [0388](0388-sl-2-prefer-the-standard-library-to-other-libraries.md) — the
  preference that makes the standard locale facilities the first candidate.
