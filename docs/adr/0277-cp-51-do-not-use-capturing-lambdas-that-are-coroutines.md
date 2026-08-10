# 0277. CP.51: Do not use capturing lambdas that are coroutines

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Usage patterns that are correct with normal lambdas are hazardous with coroutine
lambdas. The obvious pattern of capturing variables will result in accessing
freed memory after the first suspension point, even for reference-counted smart
pointers and copyable types.

A lambda results in a closure object with storage, often on the stack, that will
go out of scope at some point. When the closure object goes out of scope, the
captures go out of scope with it. Normal lambdas will have finished executing by
this time, so it is not a problem. Coroutine lambdas may resume from suspension
after the closure object has been destroyed, and at that point all captures are
use-after-free memory accesses.

```cpp
int value = get_value();
std::shared_ptr<Foo> sharedFoo = get_foo();
{
  const auto lambda = [value, sharedFoo]() -> std::future<void>
  {
    co_await something();
    // "sharedFoo" and "value" have already been destroyed
    // the "shared" pointer didn't accomplish anything
  };
  lambda();
} // the lambda closure object has now gone out of scope
```

Better is to take the data as by-value parameters instead of as captures:

```cpp
int value = get_value();
std::shared_ptr<Foo> sharedFoo = get_foo();
{
  // take as by-value parameter instead of as a capture
  const auto lambda = [](auto sharedFoo, auto value) -> std::future<void>
  {
    co_await something();
    // sharedFoo and value are still valid at this point
  };
  lambda(sharedFoo, value);
} // the lambda closure object has now gone out of scope
```

Best is to use a function for coroutines:

```cpp
std::future<void> Class::do_something(int value, std::shared_ptr<Foo> sharedFoo)
{
  co_await something();
  // sharedFoo and value are still valid at this point
}
void SomeOtherFunction()
{
  int value = get_value();
  std::shared_ptr<Foo> sharedFoo = get_foo();
  do_something(value, sharedFoo);
}
```

## Decision

Write coroutines as functions, not as capturing lambdas.

- If a callable contains `co_await`, `co_yield` or `co_return`, give it an empty
  capture list.
- Pass everything the coroutine needs as a by-value parameter, so that it lives
  in the coroutine frame rather than in the closure object.
- Prefer a named member or free function to a lambda for a coroutine body; that
  removes the closure object entirely and is the form the guidelines call best.
- Do not treat capturing a `shared_ptr` as making the capture safe. The captured
  copy dies with the closure object, and the count it held goes with it.
- Do not capture `this` in a coroutine lambda; the same lifetime problem applies
  and is harder to see.

## Rationale

The hazard is that the coroutine's frame and the closure object have different
lifetimes, and only one of them is extended by suspension. Parameters are copied
into the frame, which lives until the coroutine finishes; captures live in the
closure object, which dies at the end of its scope, typically long before.

That is why the `shared_ptr` case deserves its own sentence. Capturing a
reference-counted pointer is the standard way to keep something alive across an
asynchronous call in other settings, and here it accomplishes nothing at all —
the copy that held the count is itself in the storage that has been destroyed.

## Consequences

### Positive

- Removes an entire class of use-after-free that survives every normal
  precaution, including reference counting.
- Parameters land in the coroutine frame, so their lifetime matches the
  coroutine's by construction.
- The preferred form is a named function, which is easier to test and to name
  than a lambda anyway.

### Negative

- Local functions are exactly what lambdas exist for, so this rule takes away
  the natural way to write a small piece of asynchronous work near its use.
- Passing everything by parameter is more verbose than capturing, and repeats
  each name twice at the call site.
- Copying every input into the frame costs, and for a non-copyable input
  forces a move or a redesign.
- The rule bans a construct that is only sometimes wrong: a coroutine lambda
  that is awaited to completion within the closure's lifetime is fine, and the
  rule rejects it anyway.

### Neutral

- The by-value-parameter lambda is offered as better and the free or member
  function as best; both satisfy this record.
- This is a lifetime rule, not a concurrency rule as such; it applies to a
  coroutine whether or not another thread is involved.

## Enforcement

- Flag a lambda that is a coroutine and has a non-empty capture list.

## Related

- [0279](0279-cp-53-parameters-to-coroutines-should-not-be-passed-by-reference.md)
  — the same lifetime failure arriving through a parameter instead of a capture.
- [0278](0278-cp-52-do-not-hold-locks-or-other-synchronization-primitives-across-suspension-points.md)
  — the other thing that must not survive a suspension point.
- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — the rule that recommends lambdas, and whose advice this one overrides for
  coroutines.
- [0269](0269-cp-31-pass-small-amounts-of-data-between-threads-by-value-rather-than-by-reference-or-pointer.md)
  — passing by value across an asynchronous boundary, for the same reason.
