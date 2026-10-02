# 0270. CP.32: To share ownership between unrelated threads use shared_ptr

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

If threads are unrelated — that is, not known to be in the same scope, or one
within the lifetime of the other — and they need to share free store memory that
needs to be deleted, a `shared_ptr` or equivalent is the only safe way to ensure
proper deletion.

Three cases fall outside this. A static object, such as a global, can be shared
because it is not owned in the sense that some thread is responsible for its
deletion. An object on the free store that is never to be deleted can be shared.
And an object owned by one thread can be safely shared with another as long as
that second thread does not outlive the owner.

## Decision

Where two threads with no lifetime relationship both need an object that must
eventually be destroyed, give both of them shared ownership.

- Hold such an object through a `shared_ptr`, and give each thread its own copy
  of the `shared_ptr`, so that the last one to finish destroys the object.
- Do not pass a raw pointer or a reference to a free store object across a
  thread boundary where neither thread's lifetime is known to contain the
  other's.
- Where the lifetimes are nested — the receiving thread is joined before the
  owner's scope ends — pass a raw pointer or reference instead; shared ownership
  is not needed and the reference count is waste.
- Where the object is static, or is deliberately never destroyed, share it
  directly. Neither case has a deletion to coordinate.
- Remember that shared ownership settles only who destroys the object.
  Concurrent access to what it points at still needs synchronisation, or
  immutability.

## Rationale

The rule is narrower than it first reads: it is about deletion, not about
access. What makes `shared_ptr` the only safe answer for unrelated threads is
that no single thread can know when the others have finished, so no single
thread can be the one that deletes; the reference count is the mechanism that
answers a question nothing else can.

That also explains the exceptions. A static object and a never-deleted object
have no deletion to coordinate, and nested lifetimes already answer the question
by construction, so in all three cases the count buys nothing.

## Consequences

### Positive

- Destruction happens exactly once, after the last user, without any thread
  having to know about the others.
- It removes the shutdown-ordering problem that otherwise forces threads to be
  torn down in a particular sequence.
- The alternative for a detached or unrelated thread would be leaking the object
  deliberately, which this replaces with something checkable.

### Negative

- Reference counting costs: atomic increments and decrements on every copy, a
  control block per object, and cache traffic on shared counts.
- It is easy to over-apply. Where lifetimes are in fact nested, `shared_ptr`
  buys nothing and obscures who owns what, which is the failure the rule
  preferring `unique_ptr` warns about.
- The count protects the object, not its contents; a `shared_ptr` shared between
  threads can still be a data race on whatever it points at, and its safety can
  be over-read.
- Cycles are not collected, so shared ownership between long-lived threads can
  leak the very objects it was introduced to free.

### Neutral

- The upstream rule has no example and no enforcement clause; both are marked as
  unwritten in the source.
- "Or equivalent" leaves room for another reference-counted handle where one is
  already in use.

## Related

- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — the
  general ownership rule this specialises.
- [0160](0160-r-21-prefer-unique-ptr-over-shared-ptr-unless-you-need-to-share-ownership.md)
  — the counterweight; unrelated thread lifetimes are one of the cases that
  genuinely need sharing.
- [0266](0266-cp-24-think-of-a-thread-as-a-global-container.md) — why a
  detachable thread cannot be given a locally owned pointer.
- [0257](0257-cp-2-avoid-data-races.md) — shared ownership is not shared access;
  the contents still need synchronisation.
