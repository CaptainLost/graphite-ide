# 0093. C.101: Give a container value semantics

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Regular objects are simpler to think and reason about than irregular ones, and
they are what a reader expects. If it is meaningful for the container, make it
`Regular` — the concept. In particular, ensure that an object compares equal to
its copy.

```cpp
void f(const Sorted_vector<string>& v)
{
    Sorted_vector<string> v2 {v};
    if (v != v2)
        cout << "Behavior against reason and logic.\n";
    // ...
}
```

`Regular` is the conjunction of the operations that make a type behave like
`int`: it is default-initialisable, copyable, movable, destructible, and
equality-comparable, and the copy compares equal to the original. A container
that satisfies it can be stored in another container, returned from a function,
put in a `std::map` key position where ordering also exists, and compared in a
test without any of those uses needing to know anything special about it.

The escape hatch is deliberate. Not every container can sensibly be copied — one
holding a file handle, a socket, or a unique hardware resource has identity
rather than value, and forcing equality onto it would be a lie. The rule asks
for value semantics where they are meaningful, and a container for which they
are not is a resource handle, governed by the resource rules instead.

## Decision

Give a container value semantics unless its contents make that impossible.

- Provide copy construction and copy assignment that produce an independent
  object, and a destructor that releases everything the object owns.
- Provide `operator==` such that a copy compares equal to its original, and
  define `operator!=` consistently or let C++20 synthesise it from `==`.
- Make sure copying is deep: after `b = a`, mutating `a` must not be observable
  through `b`.
- Where copying is genuinely wrong, delete the copy operations explicitly rather
  than leaving a broken shallow copy in place, and treat the type as a resource
  handle rather than a container.

## Rationale

The point of value semantics is that it makes a whole class of questions go
away. If a copy is independent and compares equal, the reader never has to ask
whether passing the container to a function might mutate the original, and the
answer never depends on which container it is.

The example is the argument in miniature: `v != v2` immediately after
`Sorted_vector<string> v2 {v}` is not a bug in the caller's reasoning, it is a
bug in the type. A type that can produce that outcome has made every equality
comparison in the program untrustworthy.

## Consequences

### Positive

- Copies are independent, so aliasing bugs cannot arise from passing a container
  around.
- The container becomes usable as an element of another container and as a
  return value.
- Tests can compare containers directly rather than walking them element by
  element.

### Negative

- Deep copying a large container is expensive, and value semantics make that
  cost easy to incur accidentally — a stray by-value parameter copies the whole
  thing.
- `operator==` on a container is linear, and providing it invites use in places
  where the caller has not budgeted for that.
- Element types that are themselves not `Regular` prevent the container from
  being `Regular`, so the property is not one the container's author fully
  controls.

### Neutral

- Move operations are what make the copying cost tolerable in practice; see
  [0094](0094-c-102-give-a-container-move-operations.md).
- Ordering comparisons are not required by `Regular`; only equality is.

## Related

- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the general
  rule this one specialises.
- [0094](0094-c-102-give-a-container-move-operations.md) — the counterweight
  that keeps value semantics affordable.
- [0096](0096-c-104-give-a-container-a-default-constructor-that-sets-it-to-empty.md)
  — the other half of what `Regular` requires.
- [0097](0097-c-109-if-a-resource-handle-has-pointer-semantics-provide-and.md) —
  what to do when the type has identity rather than value.
