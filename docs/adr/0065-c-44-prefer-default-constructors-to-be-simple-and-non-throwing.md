# 0065. C.44: Prefer default constructors to be simple and non-throwing

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Being able to set a value to "the default" without operations that might fail
simplifies error handling and reasoning about move operations.

A default constructor that goes through the general construction path acquires
resources it does not need:

```cpp
template<typename T>
// elem points to space-elem element allocated using new
class Vector0 {
public:
    Vector0() :Vector0{0} {}
    Vector0(int n) :elem{new T[n]}, space{elem + n}, last{elem} {}
    // ...
private:
    own<T*> elem;
    T* space;
    T* last;
};
```

This is nice and general, but setting a `Vector0` to empty after an error
involves an allocation, which might fail. Also, having a default `Vector`
represented as `{new T[0], 0, 0}` seems wasteful: `Vector0<int> v[100]` costs a
hundred allocations.

A representation that has a cheap empty state avoids both problems:

```cpp
template<typename T>
// elem is nullptr or elem points to space-elem element allocated using new
class Vector1 {
public:
    // sets the representation to {nullptr, nullptr, nullptr}; doesn't throw
    Vector1() noexcept {}
    Vector1(int n) :elem{new T[n]}, space{elem + n}, last{elem} {}
    // ...
private:
    own<T*> elem = nullptr;
    T* space = nullptr;
    T* last = nullptr;
};
```

Using `{nullptr, nullptr, nullptr}` makes `Vector1{}` cheap, but it is a special
case in the representation and implies run-time checks elsewhere. Setting a
`Vector1` to empty after detecting an error is trivial.

## Decision

Make the default constructor of a type cheap and give it a non-throwing
guarantee wherever the type admits one.

- Design the representation so that there is an empty state that costs no
  allocation and no other fallible operation, and mark the default constructor
  `noexcept`.
- Do not delegate a default constructor to a general constructor that acquires
  a resource, as `Vector0() :Vector0{0} {}` does.
- Accept the run-time checks that a special empty representation implies, and
  keep the class invariant honest about it — here, the comment that `elem` is
  either null or points at allocated space is part of the design.
- Where a type genuinely has no cheap default, do not manufacture one; leave the
  type without a default constructor rather than giving it an expensive or
  throwing one.

## Rationale

The cost of a throwing default constructor is not paid at construction, it is
paid at recovery. A type whose empty state can fail to be established gives you
no reliable way to put an object back into a known state after something else
has already gone wrong, which is exactly when you need it.

The array case makes the cost visible: an allocation per default-constructed
element turns a declaration into a hundred trips to the allocator, each one a
new failure point.

## Consequences

### Positive

- Resetting an object to a known state after an error cannot itself fail.
- Arrays and containers of the type default-construct without touching the
  allocator.
- A `noexcept` default constructor participates better in the standard
  library's strong-guarantee paths and in move-related optimisations.

### Negative

- The empty state becomes a special case in the representation, so every other
  member function must handle it — the null check moves from construction into
  the rest of the class.
- The class invariant weakens from "always holds a buffer" to "holds a buffer or
  is empty", which is harder to reason about and easier to get wrong.
- Not every type has a meaningful cheap default, so the rule pushes towards
  inventing one, and an invented empty state can be worse than no default
  constructor at all.

### Neutral

- This rule says nothing about whether the type should have a default
  constructor, only about what it should cost if it does.
- The cheap empty state is usually the same state a moved-from object is left
  in, so this rule and the moved-from rule tend to be satisfied by one
  representation rather than two.

## Enforcement

- Flag throwing default constructors.

## Related

- [0080](0080-c-66-make-move-operations-noexcept.md) — the same non-throwing
  argument, applied to moves, and the reason a cheap empty state matters.
- [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
  — in-class initialisers are how the cheap empty representation is written.
- [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md)
  — the moved-from state is ideally the default value, which requires the
  default to be cheap.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — a hundred allocations for a
  hundred empty vectors is the waste this rule removes.
