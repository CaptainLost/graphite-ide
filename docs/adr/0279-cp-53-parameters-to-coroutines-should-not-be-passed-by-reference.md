# 0279. CP.53: Parameters to coroutines should not be passed by reference

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Once a coroutine reaches its first suspension point, such as a `co_await`, the
synchronous portion returns. After that point any parameters passed by reference
are dangling, and any use of them is undefined behaviour, which may include
writing to freed memory.

```cpp
std::future<int> Class::do_something(const std::shared_ptr<int>& input)
{
    co_await something();
    // DANGER: the reference to input may no longer be valid and may be freed memory
    co_return *input + 1;
}
```

Passing by value puts the parameter in the coroutine frame instead:

```cpp
std::future<int> Class::do_something(std::shared_ptr<int> input)
{
    co_await something();
    co_return *input + 1; // input is a copy that is still valid here
}
```

The problem does not apply to reference parameters that are only accessed before
the first suspension point. Subsequent changes to the function may add or move
suspension points, though, which would reintroduce this class of bug. Some types
of coroutine have their suspension point before the first line of code executes,
in which case reference parameters are always unsafe. It is safer always to pass
by value, because the copied parameter lives in the coroutine frame, which is
safe to access throughout the coroutine.

The same danger applies to output parameters. The rule preferring return values
to output parameters already discourages them; coroutines should avoid them
entirely.

## Decision

Pass every coroutine parameter by value.

- Declare parameters of a coroutine as plain by-value types, including
  parameters that would ordinarily be `const&` because they are large.
- Do not take a coroutine parameter as `T&`, `const T&` or `T&&`, and do not
  take a pointer to a caller's local.
- Do not use output parameters in a coroutine at all; return the result instead.
- Do not rely on the exception for references used only before the first
  suspension. It is correct today and is invalidated by any later edit that
  moves a `co_await` earlier.
- Where a large object must be handed to a coroutine, move it in, or pass a
  handle whose own lifetime is guaranteed, such as a `shared_ptr` taken by
  value.

## Rationale

The reference is not dangling because of a mistake at the call site; it is
dangling because the caller's frame is gone by design. The synchronous portion
of a coroutine returns at the first suspension, so the arguments it was called
with are ordinary temporaries and locals with ordinary lifetimes, and the
coroutine outlives them.

The reason to pass by value even where a reference is provably safe is that the
proof depends on where the first suspension point is, which is a property of the
body that any later edit can change — and the change that breaks it will look
like moving a line, not like introducing a use-after-free.

## Consequences

### Positive

- Parameters live in the coroutine frame, so their lifetime matches the
  coroutine's rather than the caller's.
- The rule is uniform: no analysis of where the first suspension point falls,
  and no re-analysis after every edit.
- It is mechanically checkable, unlike most lifetime rules.

### Negative

- It reverses the usual parameter-passing advice, which asks for expensive types
  to be taken by reference to const, so coroutines pay a copy that ordinary
  functions do not.
- It forbids reference parameters that are genuinely safe, including in
  coroutines that never suspend before using them.
- Non-copyable inputs must be moved in or wrapped, which changes the caller's
  ownership rather than just the signature.
- Output parameters are ruled out entirely, so an interface that used them must
  be redesigned to return a value, not merely re-annotated.

### Neutral

- The exception for references used only before the first suspension is real but
  deliberately not relied on.
- Some coroutine types suspend before the first statement executes, so no
  reference parameter is ever safe in them; the uniform rule covers that case
  without needing to know which types those are.

## Enforcement

- Flag all reference parameters to a coroutine.

## Related

- [0277](0277-cp-51-do-not-use-capturing-lambdas-that-are-coroutines.md) — the
  same lifetime failure arriving through a capture instead of a parameter.
- [0452](0452-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md)
  — output parameters are discouraged generally and forbidden here.
- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the general parameter-passing rule that this one overrides for coroutines.
- [0269](0269-cp-31-pass-small-amounts-of-data-between-threads-by-value-rather-than-by-reference-or-pointer.md)
  — passing by value across an asynchronous boundary, for a related reason.
