# 0456. F.23: Use a not_null<T> to indicate that "null" is not a valid value

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

The reason is clarity. A function with a `not_null<T>` parameter makes it clear
that the caller is responsible for any null checks that might be necessary.
Similarly, a function returning `not_null<T>` makes it clear that the caller
does not need to check the result for null.

`not_null<T*>` makes it obvious to a reader, human or machine, that a test for
null is unnecessary before dereferencing. When debugging, `owner<T*>` and
`not_null<T>` can additionally be instrumented to check for correctness.

Consider:

```cpp
int length(Record* p);
```

When I call `length(p)`, should I check whether `p` is null first? Should the
implementation of `length()` check whether `p` is null?

```cpp
// it is the caller's job to make sure p != nullptr
int length(not_null<Record*> p);
// the implementor of length() must assume that p == nullptr is possible
int length(Record* p);
```

A `not_null<T*>` is assumed not to be null; a `T*` might be. Both can be
represented in memory as a `T*`, so no run-time overhead is implied. `not_null`
is not just for built-in pointers either: it works for `unique_ptr`,
`shared_ptr`, and other pointer-like types.

This rule is the same decision as I.12, transcribed in
[0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md), viewed
from the function's side rather than the interface's. That record covers the
argument in full; what is added here is the return-value case and the extension
to smart pointers.

## Decision

Say in the type when null is not a valid value.

- Declare a pointer parameter the function dereferences unconditionally as
  `not_null<T*>`, and remove the entry check that becomes redundant.
- Declare a return type `not_null<T*>` when the function never returns null, so
  callers stop testing the result.
- Leave a parameter as a plain `T*` only when null is a value the function
  genuinely accepts and handles.
- Use `not_null` over `unique_ptr` and `shared_ptr` as well, where a smart
  pointer is never empty.
- Do not test a `not_null` against null inside a function; if that test is
  wanted, the parameter should have been a plain pointer.

## Rationale

The question the rule answers is a two-sided one, and `T*` answers neither side:
`length(p)` leaves the caller unsure whether to check and the implementer unsure
whether to. The result is that both check, or neither does, and neither outcome
is what anyone intended. `not_null` assigns the obligation once, at the type,
and because it is representationally a pointer the assignment costs nothing at
run time — the only cost is where the caller must satisfy it.

## Consequences

### Positive

- The obligation to check is assigned to exactly one side and is visible in the
  signature.
- Return values gain the same guarantee, which parameters alone could not give.
- Duplicate defensive checks along a call chain disappear together with their
  branches.
- The wrapper can be instrumented in debug builds, so the assumption is testable
  rather than merely asserted.

### Negative

- `not_null` comes from the guidelines support library, so adopting the rule
  means taking on that dependency or writing an equivalent, and the tool support
  only follows the standard one.
- It moves the check to construction rather than removing it: building a
  `not_null` from a pointer that is null fails at run time, at a place that may
  be far from where the null originated.
- The vocabulary at a boundary can become heavy — `not_null<owner<T*>>` and
  similar compositions are correct and hard to read.
- It duplicates I.12 almost exactly, so this record and
  [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) must
  be kept consistent by hand; they are one decision recorded twice because
  upstream states it twice.

### Neutral

- No run-time overhead is implied, since the representation is the pointer
  itself.
- A moved-from `not_null` is a known wrinkle in the type's design and the rule
  does not address it.

## Enforcement

- (Simple) Warn if a raw pointer is dereferenced without being tested against
  null, or an equivalent, within a function; suggest declaring it `not_null`
  instead.
- (Simple) Error if a raw pointer is sometimes dereferenced after first being
  tested against null within a function and sometimes is not.
- (Simple) Warn if a `not_null` pointer is tested against null within a
  function.

## Related

- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  same rule from the interfaces section; the fuller treatment lives there.
- [0454](0454-f-60-prefer-t-over-t-when-no-argument-is-a-valid-option.md) — when
  a reference is the better way to say the same thing.
- [0455](0455-f-22-use-t-or-owner-t-to-designate-a-single-object.md) — the
  companion annotation for ownership.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — putting the
  precondition in the type is that record's method.
