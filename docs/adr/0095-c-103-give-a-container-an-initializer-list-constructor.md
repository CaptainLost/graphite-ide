# 0095. C.103: Give a container an initializer list constructor

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

People expect to be able to initialise a container with a set of values, because
every standard container allows it. Familiarity is the reason.

```cpp
Sorted_vector<int> sv {1, 3, -1, 7, 0, 0}; // Sorted_vector sorts elements as needed
```

Without such a constructor, the same initialisation becomes a default
construction followed by a sequence of insertions, which is longer, cannot be
`const`, and separates the values from the declaration they belong to.

An `initializer_list` constructor interacts with the rest of the type's
constructors in a way worth knowing: in list-initialisation with braces it is
strongly preferred over every other constructor, so `Container<int> c{10}` gives
one element of value 10, while `Container<int> c(10)` may give ten
default-constructed elements. That asymmetry is inherited from `std::vector` and
is a source of confusion, but it is at least a familiar confusion.

The elements of an `initializer_list` are `const`, so they can be copied out of
it but not moved. A container of move-only elements therefore cannot be
populated this way, and for such a container the constructor is not meaningful.

## Decision

Give a container an `initializer_list` constructor.

- Declare `Container(std::initializer_list<value_type>)` and let it
  establish the container's invariant — sorting, deduplicating, or whatever
  the type guarantees.
- Do not mark it `explicit`; the point is that `Container<T> c {a, b, c}` and
  `= {a, b, c}` both work.
- Where the container also has a constructor taking a count or a size, be aware
  that braces will select the `initializer_list` overload, and document the
  distinction rather than trying to defeat it.
- Omit the constructor for a container whose elements are move-only, since the
  list's elements cannot be moved from.

## Rationale

This is a small rule whose value is uniformity: a container that cannot be
brace-initialised is the one type in an expression that has to be built up
statement by statement, and that difference propagates into every test and every
table of constants that uses it.

There is a correctness benefit too. A container built in one expression can be
`const`, which removes the window in which a half-populated container is visible
to the rest of the function.

## Consequences

### Positive

- Initialisation reads as one expression, and the resulting object can be
  `const`.
- The container matches how every standard container is initialised.
- Tables of test data and constants become far shorter.

### Negative

- The `initializer_list` overload wins brace-initialisation over every other
  constructor, so adding it can silently change the meaning of existing
  `Container<T> c{n}` code that meant to call a size constructor.
- Elements arrive as `const`, so they are copied rather than moved; for
  expensive element types this constructor is the slow way to build the
  container.
- It is unavailable for move-only element types, so the rule cannot be applied
  uniformly.

### Neutral

- The constructor may reorder or reject elements; it is not obliged to store
  them as given, only to establish the container's invariant.
- Whether the container also offers a variadic or range constructor is a
  separate question.

## Related

- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the general
  rule to follow the standard containers.
- [0096](0096-c-104-give-a-container-a-default-constructor-that-sets-it-to-empty.md)
  — the other constructor a container is expected to have.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — one-expression
  initialisation is what makes a `const` container possible.
