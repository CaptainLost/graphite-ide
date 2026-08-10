# 0096. C.104: Give a container a default constructor that sets it to empty

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reason is to make the container `Regular`. A regular type can be
default-constructed, and for a container the only sensible default value is the
empty one.

```cpp
vector<Sorted_sequence<string>> vs(100);    // 100 Sorted_sequences each with the value ""
```

The example shows why it matters in practice: a great deal of generic code
constructs an element without a value to give it. `std::vector`'s sizing
constructor, `std::map::operator[]`, resizing, and any algorithm that needs a
scratch object all default-construct. A container without a default constructor
cannot be used in any of them.

Empty is the right default because it is the identity for the container's own
operations: it is what you get by removing everything, it is what you have
before you insert anything, and it is cheap. A default constructor that
allocated, or that produced a container of some non-zero size, would make
`vector<Container>(100)` surprising and expensive.

The empty state also has to be a genuine value, not a partially formed one:
`size()` returns zero, `begin() == end()`, the object can be assigned to, copied
and destroyed, and it compares equal to another default-constructed container of
the same type.

## Decision

Give every container a default constructor producing an empty container.

- Declare it, and prefer `= default` where the members already default to the
  empty state.
- Make it cheap: it should not allocate and should not be able to fail.
- Mark it `noexcept` where that is honest, so that generic code can rely on it.
- Ensure the resulting object is fully usable — iterable, comparable, assignable
  — not merely destructible.
- Do not use the default-constructed state as a sentinel for "invalid" or "not
  yet loaded"; that is a different concept and belongs in a different type, such
  as an `optional`.

## Rationale

The argument that carries the most weight is not readability but reach: a type
without a default constructor is excluded from a large amount of generic code it
would otherwise work with, and each exclusion is discovered separately, at the
point of use, by someone who cannot easily fix the container.

Choosing empty rather than any other default is what makes the constructor
cheap, and cheapness is what makes it safe for library code to call it a hundred
times without asking.

## Consequences

### Positive

- The container works with `vector` sizing, `map::operator[]`, resizing and
  generic scratch-object code.
- Combined with copy and equality, it completes `Regular`.
- A non-allocating, `noexcept` default constructor is a cheap and failure-free
  starting point for any algorithm.

### Negative

- The type now has a state that carries no useful data, and every member
  function must handle it — `front()` on an empty container has to do something
  defined.
- It weakens the "a constructor produces a fully useful object" position: an
  empty container is valid but frequently not yet what the caller wants, and
  code that forgets to fill it compiles.
- For a container whose invariant genuinely requires at least one element, this
  rule cannot be followed without weakening the invariant.

### Neutral

- Whether the default constructor is `explicit` does not arise; it takes no
  arguments.
- The rule says nothing about whether a moved-from container is empty, though
  the same state is the conventional choice there.

## Related

- [0093](0093-c-101-give-a-container-value-semantics.md) — the `Regular` concept
  this rule helps satisfy.
- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the standard
  containers all behave this way.
- [0095](0095-c-103-give-a-container-an-initializer-list-constructor.md) — the
  other constructor a container is expected to provide.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — a cheap,
  non-allocating default constructor is what makes a container safe at namespace
  scope.
