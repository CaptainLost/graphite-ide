# 0390. SL.4: Use the standard library in a type-safe manner

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Breaking this rule can lead to undefined behaviour, memory corruption, and all
kinds of other bad errors. Upstream states plainly that this is a
semi-philosophical meta-rule which needs many supporting concrete rules, and
that it exists as an umbrella for the more specific ones.

The umbrella is worth having because the standard library is not uniformly safe.
It contains type-safe facilities and it contains the C standard library, raw
iterator arithmetic, `memcpy`, unchecked `operator[]`, and `reinterpret_cast`
through `void*`. Using the library is not by itself using it safely; the rule
insists on the second thing.

The concrete rules that stand under it are the rest of this section: prefer
`std::array` and `std::vector` to C arrays, avoid bounds errors, do not `memset`
a non-trivially-copyable object, use `std::string` to own characters and
`string_view` or `span<char>` to refer to them, say `zstring` when you mean a
zero-terminated string, and use `std::byte` for bytes that are not characters.

## Decision

Use standard-library facilities so that the type system can see what is going
on, and prefer the checked or typed member of any pair of alternatives.

- Prefer the container, the iterator pair, or the range over the pointer and
  length that underlie them.
- Prefer the typed operation over the byte-blitting one: assignment and
  comparison over `memcpy` and `memcmp`, `fill` over `memset`.
- Prefer the operation that carries or checks an extent over the one that does
  not, where the cost is acceptable.
- Do not cast away a standard type's identity — through `void*`,
  `reinterpret_cast`, or a union — to get at its representation.
- Treat this record as a heading: where a more specific rule in this section
  applies, that rule governs.

## Rationale

The sharpest thing upstream says is that this is a meta-rule needing supporting
concrete rules. That is an admission worth transcribing rather than hiding: a
principle at this altitude cannot be checked, so its value is as an index into
the rules that can be. Recording it keeps the specific rules from looking like
an arbitrary list.

Type safety in library use is also where undefined behaviour actually enters
most programs. Few people write a deliberate out-of-bounds write; a great many
pass the wrong length to a C function that takes a pointer and a count.

## Consequences

### Positive

- Gives the specific rules of this section a stated common purpose.
- Directs the choice at the point where the standard library offers both a safe
  and an unsafe way to do the same thing.
- Keeps type-safe use of the library on the same footing as type-safe use of
  the language.

### Negative

- The rule is unenforceable as stated; nothing can be checked against it
  directly, and a record that cannot be checked is easy to cite and hard to
  apply.
- "Type-safe" is not defined here, so in a dispute the rule supplies no
  adjudication — only the specific rules do.
- Some type-safe alternatives cost run-time checks or an extra copy, and this
  rule as written does not say when that cost is too high.

### Neutral

- Upstream's own summary of more specific rules is self-referential and lists
  only this rule; the real supporting rules are the numbered ones in the
  container, string and iostream subsections.
- The record is deliberately an umbrella and adds no requirement not already
  covered by the specific records.

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  philosophy rule this is the library-facing form of.
- [0393](0393-sl-con-3-avoid-bounds-errors.md) and
  [0394](0394-sl-con-4-dont-use-memset-or-memcpy-for-arguments-that-are-not-trivially-copyable.md)
  — two of the concrete rules that stand under this umbrella.
- [0396](0396-sl-str-2-use-std-string-view-or-gsl-span-char-to-refer-to-character-sequences.md)
  — the string-facing instance of the same idea.
- [0205](0205-es-48-avoid-casts.md) — the usual way type safety is lost when
  using the library.
