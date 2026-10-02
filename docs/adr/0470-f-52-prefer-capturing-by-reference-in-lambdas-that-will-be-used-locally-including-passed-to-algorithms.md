# 0470. F.52: Prefer capturing by reference in lambdas that will be used locally, including passed to algorithms

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

For efficiency and correctness, capture by reference is nearly always what is
wanted when a lambda is used locally. That includes writing or calling parallel
algorithms that are local because they join before returning.

The efficiency consideration is that most types are cheaper to pass by reference
than by value. The correctness consideration is that many calls want to perform
side effects on the original object at the call site, and passing by value
prevents this. Unfortunately there is no simple way to capture by reference to
const, so as to get the efficiency of a local call while also preventing side
effects.

Here a large object — a network message — is passed to an iterative algorithm,
and it is neither efficient nor correct to copy the message, which might not be
copyable at all:

```cpp
std::for_each(begin(sockets), end(sockets), [&message](auto& socket)
{
    socket.send(message);
});
```

This is a simple three-stage parallel pipeline. Each stage object encapsulates a
worker thread and a queue, has a `process` function to enqueue work, and in its
destructor automatically blocks waiting for the queue to empty before ending the
thread:

```cpp
void send_packets(buffers& bufs)
{
    stage encryptor([](buffer& b) { encrypt(b); });
    stage compressor([&](buffer& b) { compress(b); encryptor.process(b); });
    stage decorator([&](buffer& b) { decorate(b); compressor.process(b); });
    for (auto& b : bufs) { decorator.process(b); }
}  // automatically blocks waiting for pipeline to finish
```

The pipeline captures by reference and hands the lambdas to objects on another
thread, which would ordinarily be forbidden by
[0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md).
It is safe here for one reason only: each `stage` destructor blocks until its
queue drains, so every lambda has finished before the scope holding the captured
objects ends.

## Decision

Capture by reference in a lambda whose lifetime is bounded by the enclosing
scope.

- Use reference capture for a predicate or a body passed to a standard
  algorithm, to a `for_each`, or to any callee that invokes it and returns.
- Use reference capture when the lambda must mutate the enclosing scope's
  objects, and when a captured object is large or non-copyable.
- Name the captures — `[&message]` rather than `[&]` — so that the set of
  referenced objects is visible and the lifetime argument can be checked by
  reading the capture list.
- Before capturing by reference for something handed to another thread, satisfy
  yourself that a join or a blocking destructor makes the lambda's lifetime a
  subset of the captured objects' lifetimes. If you cannot state that argument
  in one sentence, capture by value.
- Take an ordinary parameter rather than a capture where the value is the thing
  being iterated over; capture is for the surrounding context.

## Rationale

The correctness argument outranks the efficiency one, and is the part worth
remembering. A by-value capture makes a copy, and side effects on that copy are
discarded silently at the end of the lambda's life — the code compiles, runs,
and does nothing, which is a worse failure than a slow copy.

The pipeline example is the sharper lesson: "local" is a claim about lifetime,
not about lexical position. Reference capture is right there despite the lambdas
running on other threads, because the destructors impose the ordering that makes
it right.

## Consequences

### Positive

- No copy is made, which is the difference between working and not working for a
  non-copyable captured object.
- Side effects reach the objects the caller intended, rather than temporaries
  that vanish.
- Capturing by reference keeps the lambda cheap to construct, which matters when
  one is created per element in a loop.

### Negative

- There is no simple way to capture by reference to const — an init-capture such
  as `[&r = std::as_const(message)]` is the nearest thing and is verbose enough
  that nobody writes it — so in practice reference capture always grants write
  access it may not need.
- The rule's safety depends entirely on a lifetime claim that the compiler does
  not check, and the boundary against
  [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  is exactly where the dangling bugs live.
- The pipeline example shows that "used locally" cannot be decided by looking at
  the lambda; it requires knowing the destructor semantics of an unrelated type,
  which no reviewer can do at a glance.
- A lambda written with reference capture is easy to make non-local later —
  storing it, returning it, deferring it — and nothing about that edit signals
  that the capture must change too.

### Neutral

- The rule is about the default choice, not an absolute one; a small value that
  the lambda should not mutate is fine to capture by value even locally.
- Whether to capture by reference is independent of whether to write a lambda at
  all, which is the subject of
  [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md).

## Enforcement

- Flag a lambda that captures by reference but is used other than locally within
  the function scope, or passed to a function by reference. This is an
  approximation, but it does flag passing by pointer, since such lambdas are
  more likely to be stored by the callee, writing to a heap location accessed
  via a parameter, returning the lambda, and similar. General lifetime rules
  would also flag escaping pointers and references, including via lambdas.

## Related

- [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  — the complementary rule, and the boundary this one stops at.
- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — when a lambda is the right tool in the first place.
- [0472](0472-f-54-if-you-capture-this-capture-all-variables-explicitly-no-default-capture.md)
  — why `[&]` inside a member function is not what it appears to be.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the copy this rule avoids.
