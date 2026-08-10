# 0118. C.147: Use dynamic_cast to a reference type when failure to find the required class is considered an error

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Casting to a reference expresses the intent to end up with a valid object, so
the cast must succeed. `dynamic_cast` to a reference type will throw
`std::bad_cast` if it does not.

```cpp
std::string f(Base& b)
{
    return dynamic_cast<Derived&>(b).to_string();
}
```

There is no null reference to test and no branch to write, because the writer of
`f` is not entertaining the possibility that `b` is not a `Derived`. If that
belief is wrong the operation fails at the point of the wrong belief, rather
than continuing on a value nobody checked.

This choice is the counterpart of casting to a pointer type, which returns null
on failure and is the right form when not finding the class is a legitimate
alternative rather than a mistake.

## Decision

Choose the target form of a `dynamic_cast` by what the failure means.

- When failure to find the class is a programming error, cast to a reference
  type and use the result directly.
- Do not write a `try`/`catch` around such a cast in order to turn it back into
  a branch; if a branch is wanted, the failure was not an error and the pointer
  form is the correct one.
- Do not cast to a pointer and then dereference without testing, which is the
  same intent expressed as undefined behaviour instead of a diagnostic.
- Where a `std::bad_cast` must not escape — a callback invoked from C, a
  thread's top-level function, any boundary an exception cannot cross — catch it
  at that boundary and translate it there, rather than changing the cast.

## Rationale

The two forms of `dynamic_cast` differ in what they say, not merely in what they
return. The reference form states a precondition; the pointer form poses a
question. Writing the pointer form where the reference form was meant produces
the worst outcome available — a null that the code was never going to check,
dereferenced into undefined behaviour — and the choice of target type is the
only place in the language where that intent can be recorded.

## Consequences

### Positive

- The intent that the cast must succeed is in the code, not in a comment.
- No null check is written, so no null check can be forgotten.
- A violated assumption produces a defined, diagnosable failure at the point of
  the cast.

### Negative

- The failure mechanism is an exception, so the form is unusable wherever
  exceptions are unavailable: it cannot cross a C ABI, and in a build with
  exceptions disabled it terminates rather than reporting. The language offers
  no non-throwing way to say "this cast must succeed".
- `std::bad_cast` carries no information about the source or target type, so the
  diagnostic is weak exactly when it matters.
- The choice between the two forms is a judgement about intent, and a reviewer
  who reads it differently from the author cannot tell which was meant.

### Neutral

- Upstream gives this rule no enforcement guidance, and none is obvious: no tool
  can decide whether a failed cast should have been an error.
- The rule presupposes that the cast itself is justified; whether hierarchy
  navigation was the right design is a separate question.

## Related

- [0119](0119-c-148-use-dynamic-cast-to-a-pointer-type-when-failure-to-find-the-required-class-is-considered-a-valid-alternative.md)
  — the other half of the same choice.
- [0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md)
  — when a `dynamic_cast` is warranted at all.
- [0024](0024-i-5-state-preconditions-if-any.md) — a reference cast is a
  precondition stated in the type system.
