# 0240. Per.3: Don't optimize something that's not performance critical

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Optimising a non-performance-critical part of a program has no effect on system
performance. This is arithmetic rather than advice: the time saved is bounded by
the time that part of the program was taking in the first place.

If a program spends most of its time waiting for the network or for a human,
optimisation of in-memory computation is probably useless. The upstream note
puts the general form plainly: if a program spends 4% of its processing time in
computation A and 40% in computation B, a 50% improvement to A is worth no more
than a 5% improvement to B. And if it is not known how much time is spent in A
or B, the earlier rules of this section apply — that ignorance is itself the
thing to fix first.

The trap is that the 4% part is often the part that is fun to optimise. It is
self-contained, it is algorithmic, it has a satisfying answer. The 40% part is
usually a mess of I/O, allocation and waiting, where the win is real but the
work is unrewarding.

## Decision

Direct optimisation effort at code that dominates the cost being complained
about.

- Establish where the time actually goes before choosing what to speed up. A
  profiler's answer beats an intuition, and usually contradicts it.
- Bound the possible gain before starting: if the target accounts for a small
  fraction of the total, the best achievable result is that small fraction.
- When the program is dominated by waiting — on disk, on the network, on the
  user, on the display's refresh — recognise that in-memory computation is not
  the critical path, and do not tighten it.
- Identify the critical path in terms of the requirement. Where a program has a
  recurring deadline — a frame to render, a request to answer — the work that
  runs on every iteration is the critical path; code that runs once at start-up
  is not on it however slow it looks.
- Leave non-critical code in its clearest form, and say so if someone proposes
  otherwise.

## Rationale

Amdahl's arithmetic is the sharpest argument in this section because it is not a
matter of judgement: a fraction of the runtime is a hard ceiling on the
improvement, and no amount of cleverness inside that fraction raises it.

The rule also protects the non-critical code. Optimisation makes code harder to
read; applying it where it cannot pay means taking the cost with no possibility
of the benefit, which is strictly worse than doing nothing.

## Consequences

### Positive

- The maximum available gain is known before the work starts, so effort can be
  refused on evidence.
- Most of the code stays in its clearest form, because most of the code is not
  critical.
- Attention moves to the parts that dominate, which are usually the parts nobody
  wanted to look at.

### Negative

- Identifying the critical part requires measurement infrastructure this record
  does not provide, and measuring badly can point confidently at the wrong code.
- "Not performance critical" is a statement about today's workload. Code that is
  cheap at current data sizes can become the hot spot at larger ones, and the
  rule gives no warning of that.
- Applied strictly, it licenses arbitrarily slow non-critical code, which
  aggregates: a thousand parts each below the threshold can still add up to a
  program that feels slow with no hot spot to point at.

### Neutral

- The rule ranks work; it does not say how much improvement is enough.
- Latency and throughput can nominate different parts as critical, and the rule
  does not choose between them.

## Related

- [0238](0238-per-1-dont-optimize-without-reason.md) — a reason to optimise is
  not yet a place to optimise.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  the measurement that identifies the critical part.
- [0250](0250-per-15-do-not-allocate-on-a-critical-branch.md) — a specific rule
  that only applies once the critical path is known.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the aggregate cost of waste in
  code that is individually non-critical.
