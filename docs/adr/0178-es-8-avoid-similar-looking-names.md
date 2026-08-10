# 0178. ES.8: Avoid similar-looking names

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Names that look alike slow comprehension down and increase the likelihood of
error, because the reader's eye resolves them by shape rather than by reading
them character by character.

```cpp
if (readable(i1 + l1 + ol + o1 + o0 + ol + o1 + I0 + l0)) surprise();
```

In most fonts, `l` and `1` and `I` are near-identical, as are `O` and `0`. There
is no way to review that expression: the only way to know what it says is to
copy it out and compare characters.

A second case is a name that looks like another name because it *is* another
name in the same scope. Do not declare a non-type with the same name as a type
in the same scope. This removes the need to disambiguate with a keyword such as
`struct` or `enum`, and it removes a source of errors, since `struct X` can
implicitly declare `X` if lookup fails:

```cpp
struct foo { int n; };
struct foo foo();       // BAD, foo is a type already in scope
struct foo x = foo();   // requires disambiguation
```

The exception is inherited: antique header files might declare non-types and
types with the same name in the same scope, and code that includes them has to
live with it.

## Decision

Choose names that differ from one another by more than a single confusable
glyph.

- Do not distinguish two names in the same scope only by `l` versus `1`, `I`
  versus `l`, `O` versus `0`, or `rn` versus `m`.
- Do not distinguish two names only by a trailing digit — `data1`, `data2` —
  when they hold different things; name the difference instead.
- Do not give a variable, a function, or an enumerator the same name as a type
  visible in the same scope.
- Do not rely on case alone to distinguish two entities that are used together.
- When a third-party header forces a collision, alias or wrap it at the boundary
  rather than propagating the ambiguity inwards.

## Rationale

Every other rule in this section assumes the reader has correctly identified
which entity a name refers to; this one protects that assumption. It is also the
only naming rule whose violations survive review intact, because a reviewer
reading `o1` and `ol` in the same expression will see two instances of the same
name and move on. Compilers do not warn: both spellings are valid identifiers
and the program means exactly what it says.

## Consequences

### Positive

- Reviewers can trust that two occurrences of a name are the same name.
- Removing type/non-type collisions removes the elaborated-type-specifier
  workaround (`struct foo x`) from the code entirely.
- Search and rename tooling gives correct results without the reader
  double-checking them.

### Negative

- Enforcement is a blacklist of confusable combinations, so it is arbitrary and
  incomplete; it cannot know that `m_count` and `n_count` are confusable in a
  specific context.
- The rule is font-dependent. Which glyphs are confusable is a property of the
  reader's editor, not of the code.
- Avoiding a near-collision sometimes forces a name that is worse on its own
  terms — longer, or less conventional than
  [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  would like.

### Neutral

- Legacy headers that collide types and non-types are an explicit exception, not
  a licence to write new code that way.
- The rule says nothing about names in unrelated scopes that happen to resemble
  each other; it is about names a reader sees together.

## Enforcement

- Check names against a list of known confusing letter and digit combinations.
- Flag a declaration of a variable, function, or enumerator that hides a class
  or enumeration declared in the same scope.

## Related

- [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  — short names have fewer characters available to differ in.
- [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) — the same confusion
  produced by nesting rather than by spelling.
- [0179](0179-es-9-avoid-all-caps-names.md) — another rule about a name's
  appearance carrying meaning.
- [0009](0009-p-3-express-intent.md) — a name that cannot be read reliably
  expresses nothing.
