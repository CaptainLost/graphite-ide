# 0560. Spell out a variable's type unless the same line already states it

Status: accepted
Date: 2026-10-05

## Context

[0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) told
the code to declare iterators and the results of functions with `auto`, on the
grounds that the type is already known to the compiler. It is not known to the
reader. `auto diff = QueryDiff(request);` followed by `if (!diff)` could be
testing a `Result` for failure, an `optional` for emptiness or a pointer for
null; each means something different, and the line does not say which. The
survey behind [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md)
counted about three hundred such declarations in the sources.

## Decision

Write the type of every variable, except where the same line already states it
or the type has no spelling.

`auto` is allowed only:

- after a cast that names the type: `static_cast<T>`, `Narrow<T>`, and the other
  named casts;
- for a factory that names the type: `CreateScope<T>`, `CreateRef<T>`,
  `std::make_unique<T>`, `std::make_shared<T>`;
- for a lambda, whose type cannot be written;
- in a structured binding: `const auto& [path, status] = entry;`.

Everything else is spelled out. In particular `Result<T>`, `std::optional<T>` and
pointers are always written, and an iterator is either written with its type or
replaced by a range-`for`.

A function template's parameter is not declared with `auto`; it gets an explicit
`template<...>` header, amending
[0324](0324-t-13-prefer-the-shorthand-notation-for-simple-single-type-argument-concepts.md).

## Rationale

The type is the first thing a reader needs to understand a variable and the one
thing `auto` takes away. Saving it from the writer at the cost of every reader is
the wrong trade for code that must be read without tools — in a diff, in a
review, in a terminal. Where the line already states the type, writing it twice
adds nothing, which is why those cases keep `auto`.

## Consequences

### Positive

- Every test of a variable — `if (!value)`, `value->`, `*value` — can be read
  correctly from the declaration line.
- A change to a function's return type breaks the declarations that relied on
  the old one, instead of silently changing what they hold.

### Negative

- Long template types are written out in full where a variable holds one; an
  alias (`using`) is the remedy where the same long type recurs.
- Changing a container's type means editing the declarations of its elements and
  iterators.

### Neutral

- Structured bindings still need `auto`; the names in the brackets carry the
  meaning instead.

## Related

- [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) — the
  principle this applies.
- Supersedes [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md).
- Amends [0324](0324-t-13-prefer-the-shorthand-notation-for-simple-single-type-argument-concepts.md).
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  the `Result` whose failure test `auto` used to hide.
