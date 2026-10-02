# 0085. C.83: For value-like types, consider providing a noexcept swap function

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A `swap` can be handy for implementing a number of idioms, from smoothly moving
objects around, to implementing assignment easily, to providing a guaranteed
commit function that enables strongly error-safe calling code. Consider using
`swap` to implement copy assignment in terms of copy construction.

```cpp
class Foo {
public:
    void swap(Foo& rhs) noexcept
    {
        m1.swap(rhs.m1);
        std::swap(m2, rhs.m2);
    }
private:
    Bar m1;
    int m2;
};
```

Provide a non-member `swap` function in the same namespace as your type, for
callers' convenience:

```cpp
void swap(Foo& a, Foo& b)
{
    a.swap(b);
}
```

## Decision

Give a value-like type a member `swap` marked `noexcept`, plus a free `swap` in
the same namespace.

- Implement the member `swap` by swapping members, using each member's own
  `swap` where it has one and `std::swap` otherwise.
- Mark the member `swap` `noexcept`; a `swap` that can fail is not usable for
  any of the purposes `swap` exists for.
- Put the free `swap(T&, T&)` in the same namespace as `T`, so that
  argument-dependent lookup finds it, and have it forward to the member.
- Use the swap in the type's own copy assignment, so that assignment gets the
  strong guarantee from the commit step being non-failing.
- Do not implement `swap` by copying through a temporary; that is
  [0086](0086-c-84-a-swap-function-must-not-fail.md)'s bad example.

## Rationale

`swap`'s value is that it is the one operation that can exchange two states
without any possibility of failure, which makes it the commit step in every
transactional idiom: build the new value off to the side where failure is
harmless, then swap it in. Remove the non-failing property and the idiom
collapses, because there is no longer a point of no return.

The free function in the same namespace is not a convenience detail. Generic
code writes `using std::swap; swap(a, b);` and relies on argument-dependent
lookup to find the type's own version; without it, the generic code silently
uses `std::swap`'s three-move fallback instead.

## Consequences

### Positive

- Copy assignment can be implemented in terms of copy construction and get the
  strong guarantee.
- Standard algorithms and containers that swap elements pick up the type's own
  version through argument-dependent lookup.
- Moving objects between containers costs a member-wise exchange rather than
  three full moves.

### Negative

- It is a second place where the member list is written out, so adding a member
  and forgetting to swap it produces a corrupted object with no diagnostic.
- The member plus free-function pair is boilerplate on every value type, much of
  which the compiler-generated move operations would have handled adequately
  through `std::swap`.
- The `noexcept` promise constrains the members: a member whose own swap can
  allocate makes the whole thing unimplementable as written, and the type has no
  good options left.

### Neutral

- The rule is scoped to value-like types; a polymorphic type suppressing copy
  and move under
  [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) has
  no business having a `swap`.
- Since C++11, `std::swap` on a movable type is already reasonably efficient, so
  this is an optimisation and an idiom-enabler rather than a necessity.

## Enforcement

- Non-trivially copyable types should provide a member `swap` or a free `swap`
  overload.
- (Simple) When a class has a `swap` member function, it should be declared
  `noexcept`.

## Related

- [0086](0086-c-84-a-swap-function-must-not-fail.md) — why the operation must not
  fail.
- [0087](0087-c-85-make-swap-noexcept.md) — the annotation that says so.
- [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md)
  — copy-and-swap, the main consumer of this function.
- [0080](0080-c-66-make-move-operations-noexcept.md) — `swap` is built from
  moves, so their `noexcept` is a precondition of this one's.
