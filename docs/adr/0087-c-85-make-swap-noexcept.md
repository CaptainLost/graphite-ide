# 0087. C.85: Make swap noexcept

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A swap must not fail. If a swap tries to exit with an exception, it is a bad
design error, and the program had better terminate.

```cpp
class Foo {
public:
    void swap(Foo& rhs) noexcept   // the annotation is not optional
    {
        m1.swap(rhs.m1);
        std::swap(m2, rhs.m2);
    }
private:
    Bar m1;
    int m2;
};
```

The rule is the annotation that goes with
[0086](0086-c-84-a-swap-function-must-not-fail.md): that record says the
operation must not fail, and this one says to state it in the declaration, where
both the compiler and the reader can act on it.

## Decision

Declare every `swap` — the member and the free overload — `noexcept`.

- Write `noexcept` on the member `swap` and on the free `swap(T&, T&)` in the
  type's namespace.
- Treat the annotation as an assertion about the implementation, not a wish:
  check that every operation the body performs is itself non-failing.
- Accept that a `swap` which does throw will terminate the program. That is the
  intended outcome; there is no recovery from a half-completed exchange.
- Do not weaken the annotation to a conditional `noexcept(...)` in order to
  accommodate a member whose swap can fail; fix the member instead, per
  [0086](0086-c-84-a-swap-function-must-not-fail.md).

## Rationale

The annotation does work that a comment cannot. `std::vector`, `std::swap`, and
the move-if-noexcept machinery inspect it and change what they do, so an
unannotated `swap` is treated as a possibly-failing one no matter how simple its
body is.

Terminating is the right response, not a harsh one. A failed swap leaves two
objects in an indeterminate state with no owner able to repair them, so
continuing the program means continuing with corrupted data — which is worse
than stopping.

## Consequences

### Positive

- The library's traits see the property, so the type takes the fast paths.
- The requirement of
  [0086](0086-c-84-a-swap-function-must-not-fail.md) is stated where the compiler
  can hold the code to it.
- A member that quietly becomes fallible shows up as a `noexcept` violation
  rather than as silent corruption.

### Negative

- The enforcement mechanism is `std::terminate`, which is a blunt instrument: a
  mistaken annotation converts a recoverable situation into an immediate process
  death with no stack unwinding and no cleanup.
- The annotation is unconditional and written by hand, so it is not rechecked
  when a member's type changes; it can become a lie without any edit to the
  function it is attached to.
- It offers nothing on its own — the annotation without the discipline of
  [0086](0086-c-84-a-swap-function-must-not-fail.md) merely relabels a failure as
  a crash.

### Neutral

- The rule and its enforcement bullet are identical to
  [0086](0086-c-84-a-swap-function-must-not-fail.md)'s; the guidelines state them
  separately because one is about the implementation and one about the
  declaration.
- The same annotation is required on move operations, for the same reasons, by
  [0080](0080-c-66-make-move-operations-noexcept.md).

## Enforcement

- (Simple) When a class has a `swap` member function, it should be declared
  `noexcept`.

## Related

- [0086](0086-c-84-a-swap-function-must-not-fail.md) — the requirement this
  annotation states.
- [0085](0085-c-83-for-value-like-types-consider-providing-a-noexcept-swap-function.md)
  — the function being annotated.
- [0080](0080-c-66-make-move-operations-noexcept.md) — the same annotation on the
  operations `swap` is built from.
- [0009](0009-p-3-express-intent.md) — `noexcept` states in the interface what
  the body already does.
