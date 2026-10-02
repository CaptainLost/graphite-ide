# 0259. CP.4: Think in terms of tasks, rather than threads

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

A thread is an implementation concept, a way of thinking about the machine. A
task is an application notion: something you would like to do, preferably
concurrently with other tasks. Application concepts are easier to reason about.

```cpp
void some_fun(const std::string& msg)
{
    std::thread publisher([=] { std::cout << msg; });      // bad: less expressive
                                                           //      and more error-prone
    auto pubtask = std::async([=] { std::cout << msg; });  // OK
    // ...
    publisher.join();
}
```

With the exception of `async()`, the standard-library facilities are low-level,
machine-oriented and at the level of threads and locks. That is a necessary
foundation, but the level of abstraction has to be raised — for productivity,
for reliability and for performance. This is a potent argument for using
higher-level, more application-oriented libraries, ideally ones built on top of
the standard-library facilities.

## Decision

Express concurrent work as the work, not as the worker.

- Name the unit of concurrency after what it does — validate the readings, sort
  the buffer, find the matches — and express it as a callable that returns its
  result.
- Spawn it with a factory such as `std::async`, or with a task or thread-pool
  abstraction, rather than by constructing a `std::thread` directly.
- Let the task return a value rather than writing into state the spawning code
  also touches.
- Where a raw thread is genuinely the right tool — a long-lived service loop, a
  thread with specific affinity or priority — keep it behind an abstraction that
  presents tasks to its callers.
- Prefer a higher-level facility over hand-rolled threads and locks whenever one
  is available.

## Rationale

The distinction is about where the correctness argument lives. A task has a
result and a completion; when you have those, the reasoning is the ordinary
reasoning about a function call, and the lifetime question answers itself. A
thread has neither, so every join, every lifetime and every hand-off becomes
something the author has to arrange and the reviewer has to re-derive.

The example makes the cost concrete: the `std::thread` version needs an explicit
`join()` that can be forgotten, misplaced or skipped by an early return, and
none of that ceremony expresses anything about the work being done.

## Consequences

### Positive

- Concurrency is described in the vocabulary of the application, so the code
  says what it is trying to achieve.
- Task facilities carry a result and a completion, which removes hand-written
  join and hand-off logic.
- It leaves room for the scheduling strategy — pool, inline, deferred — to
  change without touching the work.

### Negative

- `std::async` is the only standard facility at this level, and its launch
  policy and thread-reuse behaviour are largely unspecified, so "think in tasks"
  in standard C++ means accepting a scheduler you do not control.
- Raising the level of abstraction usually means taking a dependency on a
  library outside the standard, which is a cost of its own and one the rule does
  not price.
- The advice is genuinely unenforceable; the upstream rule has no enforcement
  clause.
- Machine-level concerns do not disappear. Something still has to decide how
  many threads exist, and pushing that behind an abstraction can hide it rather
  than solve it.

### Neutral

- The rule is about the vocabulary and the interface, not a ban on `std::thread`
  in an implementation.
- The guidelines expect this area of the standard library to keep changing, so
  the specific facility named here is less durable than the principle.

## Related

- [0280](0280-cp-60-use-a-future-to-return-a-value-from-a-concurrent-task.md) —
  the future is what gives a task its result and completion.
- [0272](0272-cp-41-minimize-thread-creation-and-destruction.md) — thinking in
  tasks is what allows a pool to serve them.
- [0267](0267-cp-25-prefer-gsl-joining-thread-over-std-thread.md) — where a raw
  thread is still used, this is the form to use.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the argument for
  reaching for a higher-level library.
