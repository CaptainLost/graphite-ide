# 0094. C.102: Give a container move operations

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Containers tend to get large. Without a move constructor and a move assignment
operator an object can be expensive to move around, which tempts people to pass
pointers to it around instead and so to get into resource-management problems.

```cpp
Sorted_vector<int> read_sorted(istream& is)
{
    vector<int> v;
    cin >> v;   // assume we have a read operation for vectors
    Sorted_vector<int> sv = v;  // sorts
    return sv;
}
```

A user can reasonably assume that returning a standard-like container is cheap.
If it is not, the code above quietly copies the whole sequence on the way out,
and the natural fix a programmer reaches for — returning a pointer, or taking an
out-parameter reference — replaces a performance problem with a lifetime one.

A container that owns a heap buffer can move in constant time: the move
constructor takes the pointer and the size, and leaves the source in a valid
empty state. This is normally what `= default` produces, provided every member
is itself movable.

## Decision

Give every container a move constructor and a move assignment operator.

- Declare both, and prefer `= default` where the members are movable and no
  invariant spans them.
- Mark both `noexcept`, so that a reallocating standard container will move
  rather than copy the elements.
- Leave the moved-from object valid and specified — empty is the conventional
  choice, and it is what a defaulted move on a `std::vector` member gives.
- Do not answer an expensive copy by returning a pointer or by adding an
  out-parameter; make the move cheap instead.

## Rationale

The argument that matters is not the cycles saved but what their absence causes:
an expensive-to-return container pushes its users towards pointer-based
interfaces, and pointer-based interfaces are where ownership bugs live. A cheap
move keeps the safe style also the fast style, so no one has to choose.

`noexcept` is the part most easily missed. `std::vector` will only move its
elements during reallocation if their move constructor is `noexcept`; otherwise
it copies, to preserve the strong exception guarantee. A movable container
without `noexcept` therefore still gets copied in exactly the situation the move
operations were added for.

## Consequences

### Positive

- Returning a container by value is cheap, so functions can have honest
  signatures.
- Values can be handed across a boundary without sharing ownership.
- `noexcept` moves make the container efficient as an element of another
  container.

### Negative

- Two more special members to write, test and keep in step with the copy
  operations, and declaring them is not side-effect free: a user-declared move
  constructor or move assignment operator deletes the implicitly declared copy
  operations, so a container given moves and nothing else silently stops being
  copyable.
- `noexcept` is a promise: a container whose move must allocate — one with a
  small-buffer optimisation, for instance — cannot honestly make it, and gets no
  benefit from this rule in reallocation.
- The moved-from state is an extra state the type must document and every member
  function must tolerate, which is a real widening of the invariant.

### Neutral

- Move operations do not replace copy operations; a container should normally
  have both, per
  [0093](0093-c-101-give-a-container-value-semantics.md).
- Guaranteed copy elision in C++20 already removes the copy in some return
  cases, but not in assignment or in reallocation.

## Related

- [0093](0093-c-101-give-a-container-value-semantics.md) — move operations are
  what make value semantics affordable.
- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the standard
  containers are the model being followed.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the failure mode this rule exists to prevent.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — a needless deep copy is
  waste.
