# 0268. CP.26: Don't detach() a thread

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Often the need to outlive the scope of its creation is inherent in a thread's
task, but implementing that idea by detaching makes it harder to monitor and
communicate with the thread. In particular it becomes harder, though not
impossible, to ensure that the thread completed as expected or lives for as long
as expected.

```cpp
void heartbeat();
void use()
{
    std::thread t(heartbeat);             // don't join; heartbeat is meant to run forever
    t.detach();
    // ...
}
```

This is a reasonable use of a thread, and one for which `detach()` is commonly
used. There are problems, though. How do we monitor the detached thread to see
if it is alive? Something might go wrong with the heartbeat, and losing a
heartbeat can be very serious in a system that needs one. So we need to
communicate with the heartbeat thread, for example through a stream of messages
or notification events using a condition variable.

An alternative, and usually superior, solution is to control the thread's
lifetime by placing it in a scope outside its point of creation or activation:

```cpp
void heartbeat();
gsl::joining_thread t(heartbeat);             // heartbeat is meant to run "forever"
```

This heartbeat will, barring error, hardware problems and the like, run for as
long as the program does. Sometimes we need to separate the point of creation
from the point of ownership:

```cpp
void heartbeat();
unique_ptr<gsl::joining_thread> tick_tock {nullptr};
void use()
{
    // heartbeat is meant to run as long as tick_tock lives
    tick_tock = make_unique<gsl::joining_thread>(heartbeat);
    // ...
}
```

## Decision

Do not call `detach()`. Give a long-running thread a longer-lived owner instead.

- Where a thread must outlive the scope that starts it, declare the joining
  thread object in an enclosing scope, or hold it in a `unique_ptr` owned by
  something that lives long enough.
- Keep a channel to any long-lived thread — a message queue, a condition
  variable, a status flag — so the program can tell whether it is still running
  and can ask it to stop.
- Separate the point of creation from the point of ownership by assigning into a
  longer-lived handle, rather than by cutting the thread loose.
- Where an existing detached thread must be kept, treat it under the rule for a
  thread as a global container, and pass it nothing with local lifetime.

## Rationale

Detaching is not a way of giving a thread a long life; it is a way of discarding
the only handle you had to it. The lifetime you wanted is still whatever the
thread's own loop decides, and now nothing can observe it, join it, or ask it to
stop.

The heartbeat example is the strongest form of this, because it is the case
where detaching looks most defensible. A heartbeat that has silently died is
worse than no heartbeat, and the detached form is precisely the one in which
nobody finds out.

## Consequences

### Positive

- The program retains a handle, so a long-lived thread can be monitored and
  asked to stop.
- Shutdown becomes orderly: the owner's destruction joins the thread rather than
  leaving it racing the destruction of static objects.
- The lifetime is stated by where the object lives, which is visible in the
  source.

### Negative

- Ownership has to be placed somewhere, and for a program-lifetime thread that
  usually means a global or a long-lived handle, which is the kind of state
  other rules discourage.
- Joining at the owner's destruction blocks, so any thread owned this way needs
  a stop mechanism that did not exist when it was merely detached; the rule
  therefore costs more code than it removes.
- It cannot be applied to threads created inside third-party libraries.
- A thread genuinely meant to run to process exit gains nothing from being
  joinable except the obligation to shut it down cleanly.

### Neutral

- The rule is about the handle, not about how long the thread runs; immortal
  threads remain legitimate.
- Communicating with a long-lived thread is presented as necessary in any case,
  so part of the cost here would have been paid regardless.

## Enforcement

- Flag `detach()`.

## Related

- [0267](0267-cp-25-prefer-gsl-joining-thread-over-std-thread.md) — the type
  that cannot detach, which is how this rule is made structural.
- [0266](0266-cp-24-think-of-a-thread-as-a-global-container.md) — what you are
  left with when a thread does detach.
- [0265](0265-cp-23-think-of-a-joining-thread-as-a-scoped-container.md) — the
  reasoning this rule preserves.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — the
  `unique_ptr` that separates creation from ownership.
