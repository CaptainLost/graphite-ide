# 0288. CP.201: Signals

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

Upstream supplies a title and a placeholder. There is no Reason, no example and
no Enforcement to transcribe. The only substance the guidelines offer is a note
to themselves that the rule, when written, should remind the reader how little
is async-signal-safe, and that the best way to communicate with a signal handler
is probably not to communicate with one at all.

## Decision

Treat UNIX signal handling as territory the guidelines have not covered. Assume
that almost nothing is safe to call from a signal handler, and prefer a design
in which the handler communicates with the rest of the program as little as
possible — ideally not at all.

## Rationale

Both halves of that instruction are all the source states, and neither is
argued. They are recorded here because the placeholder names them, not because
this record can justify them.

## Consequences

### Positive

- Names signals as an area needing its own decision, rather than leaving the gap
  silent.
- The default it suggests — no communication with the handler — is the most
  conservative one available, so following it cannot make the situation worse.

### Negative

- The record rests on a placeholder rather than on a stated rule, so it cannot
  be checked against upstream reasoning, and the guidelines may yet say
  something different.
- It gives no list of what is async-signal-safe, so "almost nothing" is not
  actionable without a platform manual.
- Advising that a handler communicate with nothing leaves the actual problem —
  getting a signal's arrival to the program — unanswered.
- There is no enforcement, and none is possible from this much text.

### Neutral

- C++ says what a handler may touch, but which library functions are
  async-signal-safe is settled by the operating system, so any real guidance
  would be per-platform.
- Should upstream fill the rule in, this record should be rewritten rather than
  extended.

## Related

- [0287](0287-cp-200-use-volatile-only-to-talk-to-non-c-memory.md) — the other
  rule in the same "etc." group, and the neighbouring case of memory outside the
  C++ model.
- [0008](0008-p-2-write-in-iso-standard-c.md) — signal handling is where a
  program leaves the standard for the operating system.
- [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  — the "communicate as little as possible" advice exists for the same reason:
  the safe subset is very small.
