# 0471. F.53: Avoid capturing by reference in lambdas that will be used non-locally, including returned, stored on the heap, or passed to another thread

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Pointers and references to locals should not outlive their scope. A lambda that
captures by reference is just another place to store a reference to a local
object, and should not do so if the lambda, or a copy of it, outlives the scope.

```cpp
// bad
int local = 42;
// Want a reference to local.
// Note, that after program exits this scope,
// local no longer exists, therefore
// process() call will have undefined behavior!
thread_pool.queue_work([&] { process(local); });
```

```cpp
// good
int local = 42;
// Want a copy of local.
// Since a copy of local is made, it will
// always be available for the call.
thread_pool.queue_work([=] { process(local); });
```

The queued work runs at a time the enclosing function does not control. By then
the frame holding `local` may be gone, and the reference capture is a dangling
reference stored inside a callable object — the same defect as returning the
address of a local, hidden inside a closure.

## Decision

Capture by value in any lambda that can outlive the scope in which it is
written.

- Capture by value when the lambda is returned, stored in a member or a
  container, put on the heap, kept in a `std::function`, queued to a thread
  pool, or passed to another thread that is not joined before the scope ends.
- Name the captures rather than writing `[=]`, so that what is copied is visible
  and the copies can be justified individually.
- Move into the capture — `[data = std::move(data)]` — where the captured object
  is expensive to copy or is move-only; that keeps the lambda self-contained
  without paying for a copy.
- Capture an owning `shared_ptr` by value, rather than a raw reference, when the
  lambda needs an object whose lifetime it must extend.
- Do not capture `this` by value in a lambda that outlives the object; `[=]` and
  `[&]` in a member function both capture `this`, and it dangles like any other
  pointer. See
  [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md).
- Where the escape is genuinely bounded by a join or a blocking destructor, the
  local rule
  [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  applies instead, and the reason should be written down at the call site.

## Rationale

The reason this needs saying separately from
[0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md)
is that a capture list does not look like taking an address. `[&]` is two
characters, appears at the top of an expression rather than at a `return`, and
gives no visual cue that a reference to a local has just been stored in an
object that will travel elsewhere.

The two examples differ by one character, and the difference is between a
correct program and undefined behaviour with no diagnostic. That asymmetry is
the argument for making value capture the default at every escape point, rather
than deciding case by case.

## Consequences

### Positive

- A lambda that leaves its scope carries everything it needs, so it is correct
  wherever and whenever it eventually runs.
- The dangling case that no compiler warns about is removed by a rule that can
  be applied by looking at the capture list and the destination.
- Init-captures with `std::move` give the same self-containment for expensive or
  move-only state, so the rule does not force a copy in the cases that would
  hurt.

### Negative

- Copying can be expensive or impossible, and for a non-copyable captured object
  the rule leaves only move capture, which changes the enclosing scope's object
  in a way its later code may not expect.
- Value capture defeats the point where the lambda is supposed to write back to
  the caller's state; that requirement then has to be met by some other channel,
  such as a shared object or a promise.
- "Used non-locally" is a property of the destination, not of the lambda, so the
  rule cannot be applied while reading the lambda alone, and refactoring a local
  lambda into a stored one silently invalidates its capture list.
- `[=]` in a member function does not copy the data members; it copies `this`.
  A programmer following this rule literally can still produce a dangling
  capture, which is why
  [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md)
  exists.

### Neutral

- The rule is about escape, not about threads; a lambda stored in a member of a
  single-threaded object has left its scope just as surely as one queued to a
  pool, and is covered here for the same reason.
- The upstream examples use `[&]` and `[=]`, which this record otherwise
  discourages in favour of named captures; the defaults are used there to make
  the contrast visible.

## Enforcement

- (Simple) Warn when a capture list contains a reference to a locally declared
  variable.
- (Complex) Flag when a capture list contains a reference to a locally declared
  variable and the lambda is passed to a non-const and non-local context.

## Related

- [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  — the complementary rule for lambdas that stay in scope.
- [0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md)
  — the general prohibition this is a specific, well-disguised instance of.
- [0014](0014-p-8-do-not-leak-any-resources.md) — capture lifetime is a resource
  lifetime, and a closure that outlives its captures has broken it.
- [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md)
  — the member-function case where value capture is not what it looks like.
