# 0286. CP.111: Use a conventional pattern if you really need double-checked locking

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Double-checked locking is easy to mess up. If you really need to write your own,
in spite of
[0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md)
and
[0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md),
then do it in a conventional pattern.

The uses of the double-checked locking pattern that are not violations of
[0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md)
arise when a non-thread-safe action is both hard and rare, and there exists a
fast thread-safe test that can be used to guarantee that the action is *not*
needed, but cannot be used to guarantee the converse.

The bad version uses `volatile`, which does not make the first check
thread-safe:

```cpp
// bad
mutex action_mutex;
volatile bool action_needed;

if (action_needed) {
    std::lock_guard<std::mutex> lock(action_mutex);
    if (action_needed) {
        take_action();
        action_needed = false;
    }
}
```

The good version differs in one declaration:

```cpp
// good
mutex action_mutex;
atomic<bool> action_needed;

if (action_needed) {
    std::lock_guard<std::mutex> lock(action_mutex);
    if (action_needed) {
        take_action();
        action_needed = false;
    }
}
```

Fine-tuned memory order might be beneficial where an acquire load is more
efficient than a sequentially consistent load:

```cpp
mutex action_mutex;
atomic<bool> action_needed;

if (action_needed.load(memory_order_acquire)) {
    lock_guard<std::mutex> lock(action_mutex);
    if (action_needed.load(memory_order_relaxed)) {
        take_action();
        action_needed.store(false, memory_order_release);
    }
}
```

## Decision

Apply the pattern only where it is genuinely warranted, and then write it the
conventional way.

- Establish first that the action is hard and rare, and that the fast test can
  prove the action unnecessary even though it cannot prove the reverse. Without
  that, the case belongs to
  [0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md).
- Declare the flag `atomic<bool>`. Never `volatile`: `volatile` does not make
  the unlocked check thread-safe.
- Test the flag, take the lock, test it again inside the lock, act, and clear
  the flag under the lock — in that shape, without variation.
- Leave the ordering at the default sequentially consistent one unless a
  measurement shows the acquire form pays; if you do depart from it, use exactly
  the acquire/relaxed/release arrangement shown above.
- Write it once and keep it in one place, rather than repeating the sequence
  wherever a rare action is guarded.

## Rationale

The difference between the bad and the good example is a single type name, and
nothing else in the code shows which one you are looking at — that is why the
rule insists on a conventional shape rather than on reasoning each instance out.
A pattern written the same way every time can be recognised on sight and
reviewed against a known-good original; a pattern reasoned out afresh has to
have its memory model argument re-derived by every reader, and the version
without an argument is the one that ships.

## Consequences

### Positive

- The `volatile` mistake, which is the mistake this pattern attracts, is ruled
  out by name.
- A fixed shape is reviewable: a reader compares against the canonical form
  rather than reconstructing the proof.
- Restricting the fine-tuned ordering to one published arrangement keeps the
  hardest part of the code from being improvised.
- The preconditions — hard, rare, one-sided fast test — give a test for whether
  the pattern is warranted at all.

### Negative

- This is still lock-free code in the unlocked check, so
  [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  and [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) remain
  in force, including the re-testing obligation.
- The memory-order variant reintroduces exactly the reasoning the surrounding
  rules tell you to avoid, and offers it as a performance tweak without saying
  how to verify it.
- The precondition "can guarantee the action is not needed, but not the
  converse" is a property of the caller's data. No compiler and no reviewer can
  check it from the code alone.
- The guidelines give no enforcement for this rule and doubt one is possible, so
  a wrong instance is caught by review or not at all.

### Neutral

- The rule permits the pattern; it does not recommend it. The prior question is
  always whether
  [0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md)
  already covers the case.
- The flag is `atomic` for the unlocked read; the mutex is still what makes the
  action itself safe.

## Related

- [0285](0285-cp-110-do-not-write-your-own-double-checked-locking-for-initialization.md)
  — the rule this one carves an exception out of.
- [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  — the unlocked check is lock-free programming, with everything that implies.
- [0287](0287-cp-200-use-volatile-only-to-talk-to-non-c-memory.md) — why the bad
  example's `volatile` buys nothing here.
- [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) — the
  fine-tuned ordering is a platform bet and has to be re-measured.
