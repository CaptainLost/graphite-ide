# 0078. C.64: A move operation should move and leave its source in a valid state

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

That is the generally assumed semantics. After `y = std::move(x)` the value of
`y` should be the value `x` had, and `x` should be in a valid state.

```cpp
class X {   // OK: value semantics
public:
    X();
    X(X&& a) noexcept;  // move X
    X& operator=(X&& a) noexcept; // move-assign X
    void modify();     // change the value of X
    // ...
    ~X() { delete[] p; }
private:
    T* p;
    int sz;
};

X::X(X&& a) noexcept
    :p{a.p}, sz{a.sz}  // steal representation
{
    a.p = nullptr;     // set to "empty"
    a.sz = 0;
}

void use()
{
    X x{};
    // ...
    X y = std::move(x);
    x = X{};   // OK
} // OK: x can be destroyed
```

Ideally the moved-from state should be the default value of the type; ensure
that unless there is an exceptionally good reason not to. However, not all types
have a default value, and for some types establishing the default value can be
expensive. The standard requires only that the moved-from object can be
destroyed. Often we can easily and cheaply do better: the standard library
assumes that it is possible to assign to a moved-from object. Always leave the
moved-from object in some specified valid state.

Unless there is an exceptionally strong reason not to, make
`x = std::move(y); y = z;` work with the conventional semantics.

## Decision

Make a move transfer the value, and leave the source in a state that is both
valid and written down.

- After the move, the target holds the value the source had; do not "move" by
  copying and leaving the source untouched.
- Set the source to its default value where the type has a cheap one — the
  empty representation of
  [0065](0065-c-44-prefer-default-constructors-to-be-simple-and-non-throwing.md)
  is exactly what is wanted here.
- Where the default is not available or not cheap, specify the moved-from state
  explicitly in a comment on the class, and make sure it supports destruction
  and assignment at a minimum.
- Do not leave the source holding a resource the target now also holds; null the
  source's pointers and zero its sizes as part of the move.
- Make `x = std::move(y); y = z;` work: assigning to a moved-from object is
  ordinary code, not a special case.

## Rationale

The standard's own requirement — that a moved-from object be destructible — is
weaker than what real code needs, because real code assigns to moved-from
objects routinely and generic algorithms do so without asking. A type that meets
only the standard's minimum will work until it meets an algorithm that
reassigns, and then fail somewhere with no connection to the move operation.

Leaving the source with a copy of the pointer it just gave away is the specific
error the rule exists to stop: both objects run their destructors, and the
buffer is freed twice.

## Consequences

### Positive

- Moved-from objects can be reassigned and reused, so callers do not have to
  track which variables have been moved out of.
- Double frees from a shared stolen representation are ruled out by
  construction.
- The moved-from state being the default value makes the type predictable
  without the reader consulting the move operation.

### Negative

- Nulling the source costs stores that a pure steal would not need, so the move
  is slightly more expensive than the minimum the standard permits.
- Types without a cheap default have to invent and document a moved-from state,
  which is a second valid representation that every member function must then
  tolerate.
- A valid-but-unspecified source is a state the type's invariant must admit, so
  the invariant weakens for every object, not only moved-from ones.

### Neutral

- The standard library guarantees only "valid but unspecified" for its own
  containers; this rule deliberately demands more, and the guideline notes that
  no counterexample to the looser rule has surfaced in a decade of use.
- Whether the moved-from state equals the default value is a per-type decision;
  what is not optional is that it be specified.

## Enforcement

- (Not enforceable) Look for assignments to members in the move operation. If
  there is a default constructor, compare those assignments to the
  initialisations in the default constructor.

## Related

- [0075](0075-c-61-a-copy-operation-should-copy.md) — the same kind of obligation
  for copies.
- [0065](0065-c-44-prefer-default-constructors-to-be-simple-and-non-throwing.md)
  — a cheap default is what makes the ideal moved-from state affordable.
- [0080](0080-c-66-make-move-operations-noexcept.md) — a move that steals and
  nulls has nothing left that can fail.
- [0079](0079-c-65-make-move-assignment-safe-for-self-assignment.md) — the
  aliased case of this operation.
