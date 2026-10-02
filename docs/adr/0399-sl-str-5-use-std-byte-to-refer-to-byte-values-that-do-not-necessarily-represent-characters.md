# 0399. SL.str.5: Use std::byte to refer to byte values that do not necessarily represent characters

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Use of `char*` to represent a pointer to something that is not necessarily a
character causes confusion and disables valuable optimisations. `std::byte` was
added in C++17 for exactly this case.

Upstream supplies no example for this rule; the reason and the note that the
type is a C++17 facility are the whole of its content. The surrounding rules
supply the context it needs: `char*` is already overloaded with the meanings
"one character", "an array of characters", and "a zero-terminated string", and
using it for a fourth meaning — a pointer to memory whose contents are not text
at all — makes the other three harder to tell apart.

`std::byte` is an enumeration type with `unsigned char` as its underlying type.
It supports the bitwise operators and `std::to_integer`, and nothing else: no
arithmetic, no character semantics, no accidental promotion into an `int`
expression that was written for numbers.

## Decision

Say `std::byte` when the values are bytes rather than text.

- Declare buffers of raw memory, serialised data, file contents that are not
  known to be text, and hashes as `std::byte`, not `char` or `unsigned char`.
- Pass them as `span<std::byte>` or `span<const std::byte>`.
- Reserve `char` and its aliases for sequences that are meant to be read as
  text.
- Convert explicitly with `std::to_integer<T>` where a numeric value of a byte
  is genuinely wanted, rather than relying on integral promotion.

## Rationale

The type states the question the code is answering. A `char` buffer invites
character operations — comparison against literals, printing, `strlen` — on data
that has no such meaning, and those operations compile. `std::byte` has no such
operations, so the wrong use does not compile rather than producing a wrong
answer.

## Consequences

### Positive

- Text and non-text sequences become distinguishable in signatures.
- Arithmetic and character operations on raw data stop compiling, which is
  precisely the class of mistake the type exists to prevent.
- The signed-ness of `char`, which is implementation-defined, stops mattering
  for data that is not text.

### Negative

- Almost every existing API for raw memory — the C library, platform I/O,
  third-party libraries — is expressed in `char*`, `unsigned char*` or `void*`,
  so a `std::byte` buffer needs a cast at every boundary, and those casts are
  the thing the codebase was trying to avoid.
- `std::byte` supports so little that even printing one for a debug trace needs
  an explicit conversion.
- The optimisation half of the upstream reason is weaker than it reads: the
  standard grants `std::byte` the same permission to alias any object that
  `char` and `unsigned char` have, so the gain is in expressed intent rather
  than in aliasing.
- Upstream gives no example and no enforcement, so the rule's boundary — how
  much of a mixed text-and-binary format counts as bytes — is left unstated.

### Neutral

- The rule depends on C++17, which is when `std::byte` was introduced; it is
  simply unavailable to code held to an earlier standard.
- It concerns which type names a byte, not how the bytes are stored or owned.

## Related

- [0397](0397-sl-str-3-use-zstring-or-czstring-to-refer-to-a-c-style-zero-terminated-sequence-of-characters.md)
  — names `std::byte*` as one of the four meanings to be separated.
- [0398](0398-sl-str-4-use-char-to-refer-to-a-single-character.md) — the
  companion rule that narrows what `char*` itself means.
- [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md) — the
  neighbouring rule about which type to compute on.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the general
  case for a type that admits only the intended operations.
