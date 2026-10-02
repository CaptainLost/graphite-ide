# 0258. CP.3: Minimize explicit sharing of writable data

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

If you do not share writable data, you cannot have a data race. The less
sharing you do, the less chance you have of forgetting to synchronise an access
and getting a race, and the less chance you have of waiting on a lock, so
performance can improve as well.

```cpp
bool validate(const vector<Reading>&);
Graph<Temp_node> temperature_gradients(const vector<Reading>&);
Image altitude_map(const vector<Reading>&);
// ...
void process_readings(const vector<Reading>& surface_readings)
{
    auto h1 = async([&] { if (!validate(surface_readings)) throw Invalid_data{}; });
    auto h2 = async([&] { return temperature_gradients(surface_readings); });
    auto h3 = async([&] { return altitude_map(surface_readings); });
    // ...
    h1.get();
    auto v2 = h2.get();
    auto v3 = h3.get();
    // ...
}
```

Without those `const`s, every asynchronously invoked function would have to be
reviewed for potential data races on `surface_readings`. Making
`surface_readings` const with respect to this function allows the reader to
reason using only the function body.

Immutable data can be safely and efficiently shared. No locking is needed,
because there can be no data race on a constant.

## Decision

Share as little writable data between threads as the design allows.

- Share data by `const` reference wherever a task only reads it. The `const` is
  the part that makes the sharing reviewable.
- Prefer giving each task its own copy of small data over giving several tasks a
  reference to one mutable object.
- Prefer returning a result from a task over having the task write into shared
  state; collect results at the join point instead.
- Where writable state must be shared, keep the shared region as small as
  possible — one object rather than an aggregate — so the synchronisation has a
  named subject.
- Prefer message passing between tasks to a shared writable buffer that both
  ends touch.

## Rationale

The value of the `const` in the example is not that it prevents a write; it is
that it bounds the review. With it, deciding that `process_readings` is
race-free requires reading `process_readings`. Without it, it requires reading
`validate`, `temperature_gradients`, `altitude_map` and everything they call.

That change in the cost of the argument is what makes this rule worth following
even where no race exists yet: the alternative is a proof obligation that grows
with the program.

## Consequences

### Positive

- Removes whole classes of race by construction rather than by locking.
- Reduces lock contention and waiting, so it can improve throughput as well as
  correctness.
- Confines the reasoning about a function's thread safety to that function.

### Negative

- Not sharing usually means copying, which costs memory and time; for large data
  that cost can exceed what the locking would have cost.
- Some workloads genuinely are shared mutable state — an in-memory document
  edited from more than one place — and the rule then only says "less", not how
  much less.
- `const` on a reference parameter constrains this function, not the object; if
  another thread holds a non-const path to the same object, the reasoning the
  `const` bought is void.
- Collecting results at a join point serialises where the shared write did not,
  so the design can lose parallelism it wanted.

### Neutral

- The rule is about explicit sharing. Data reachable from several threads by
  accident is covered by the rule against data races, not by this one.
- "Minimize" is a direction, not a threshold; the record does not set one
  because none exists.

## Related

- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — immutable data
  can be shared without locking, because there can be no race on a constant.
- [0257](0257-cp-2-avoid-data-races.md) — the failure this rule avoids by
  construction.
- [0269](0269-cp-31-pass-small-amounts-of-data-between-threads-by-value-rather-than-by-reference-or-pointer.md)
  — copying small data is the concrete form of not sharing it.
- [0280](0280-cp-60-use-a-future-to-return-a-value-from-a-concurrent-task.md) —
  returning a result rather than writing into shared state.
