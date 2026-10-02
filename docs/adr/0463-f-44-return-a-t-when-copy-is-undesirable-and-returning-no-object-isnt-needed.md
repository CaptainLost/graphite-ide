# 0463. F.44: Return a T& when copy is undesirable and "returning no object" isn't needed

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

The language guarantees that a `T&` refers to an object, so testing for
`nullptr` is not necessary. Where a function must give the caller access to an
object it does not copy, and where there is always such an object to give, a
reference says so and a pointer does not.

```cpp
class Car
{
    array<wheel, 4> w;
    // ...
public:
    wheel& get_wheel(int i) { Expects(i < w.size()); return w[i]; }
    // ...
};

void use()
{
    Car c;
    wheel& w0 = c.get_wheel(0); // w0 has the same lifetime as c
}
```

`get_wheel` cannot fail to produce a wheel: the precondition on `i` guarantees
one exists, and the object returned lives as long as the `Car` does. Returning
`wheel*` would oblige every caller to consider a `nullptr` that can never occur.

The return of a reference must not imply a transfer of ownership. The caller
gets access to the object, not responsibility for it, and the referent must
outlive the call — which rules out any local, per
[0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md).

## Decision

Return `T&` where a copy is undesirable and there is always an object to return.

- Return `T&` — or `const T&` where the caller must not modify — for accessors
  that expose a subobject of something the callee owns and that outlives the
  call.
- State the precondition that guarantees the object exists, per
  [0024](0024-i-5-state-preconditions-if-any.md), rather than degrading the
  return type to a pointer so that the failure case can be signalled.
- Return `T*` instead only when "no object" is a legitimate answer, per
  [0461](0461-f-42-return-a-t-to-indicate-a-position-only.md).
- Return by value when the object is cheap to copy, or when the caller needs an
  independent copy; the reference is for when copying is undesirable, not for
  when it is merely avoidable.
- Never return a reference to a local, and never let a returned reference imply
  that the caller now owns the referent.

## Rationale

The strongest argument is not efficiency but the removal of an unanswerable
question. A `T*` return forces every call site to decide what to do about null;
if null is impossible, that decision is dead code that must nevertheless be
written, read, and maintained, and is usually written as an unchecked
dereference anyway.

A reference states the guarantee in the type, so the caller does not have to
trust a comment or read the implementation, and the compiler will not let a
`nullptr` in through that hole later.

## Consequences

### Positive

- The caller writes no null check, because the type says one is not needed.
- No copy is made, which matters for the large or non-copyable objects this rule
  is aimed at.
- The intent of an accessor becomes legible: `T&` is access, `T` is a copy,
  `T*` is a possibly-absent position.

### Negative

- A returned reference is a lifetime obligation the type does not express. The
  caller must keep the owner alive, and nothing checks that they do; this is
  exactly the hole `w0` in the example sits over.
- It exposes the representation. Handing out `wheel&` lets a caller mutate the
  `Car`'s internals, which weakens the encapsulation the class exists for.
- The guarantee rests on a precondition — `Expects(i < w.size())` — whose
  violation is undefined behaviour, so the null check has not been removed so
  much as moved to a place where failure is worse.
- Returning a reference makes the function difficult to change later; adding a
  failure case means changing the return type and every call site.

### Neutral

- Whether the reference should be `const` is a separate question from whether it
  should be a reference, and is governed by
  [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md).
- The rule is silent on rvalue references, which
  [0464](0464-f-45-dont-return-a-t.md) rules out separately.

## Enforcement

- Flag functions where no return expression could yield `nullptr`.

## Related

- [0461](0461-f-42-return-a-t-to-indicate-a-position-only.md) — the pointer form,
  for when "no object" is a valid answer.
- [0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md)
  — the referent must outlive the call.
- [0024](0024-i-5-state-preconditions-if-any.md) — the precondition that makes
  the reference safe to return.
- [0464](0464-f-45-dont-return-a-t.md) — the reference kind that must not be
  returned.
