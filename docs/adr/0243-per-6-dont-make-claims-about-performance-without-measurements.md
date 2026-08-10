# 0243. Per.6: Don't make claims about performance without measurements

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

The field of performance is littered with myth and bogus folklore. Modern
hardware and optimisers defy naive assumptions, and even experts are regularly
surprised. Out-of-order execution, branch prediction, three or four levels of
cache, and an optimiser willing to delete work entirely mean that reasoning from
the source text to the running time is unreliable in both directions.

Getting good performance measurements can be hard and requires specialised
tools. That is a caution, not an excuse: a few simple microbenchmarks using the
Unix `time` command or the standard library's `<chrono>` will dispel the most
obvious myths. If a complete system cannot be measured accurately, at least a
few key operations and algorithms can be. A profiler will say which parts of the
system are performance critical, and the answer is often surprising.

Microbenchmarks have their own failure modes, and they are severe enough to
mention: an optimiser that can see the result is unused will delete the work
being timed; a loop run on the same warm data measures cache behaviour that the
real program will never enjoy; and a measurement taken in a debug build measures
a program nobody ships. A benchmark that has not been checked against these is
capable of producing a confident number that means nothing.

## Decision

State performance as a measurement or not at all.

- Do not write "this is faster", in a comment, a commit message, a review, or a
  record, without a number and the conditions it was taken under.
- Measure the built product, not a debug configuration. Optimisation is what the
  claim is about.
- Use a profiler to find where the time goes before deciding what to change, and
  use timings to show that the change helped.
- Report what was measured, on what input, on what machine, and with what
  variance. A single run of a fast operation is noise.
- Check that a microbenchmark is measuring something: that the result is
  consumed, that the input is not degenerate, and that the numbers change when
  the code changes.
- Where the measurement is genuinely infeasible, say the claim is unmeasured
  rather than dropping the qualifier.

## Rationale

This rule is the one that makes the rest of the section usable. Every other rule
here is conditional on knowing something about the program's actual behaviour,
and without measurement that knowledge is replaced by folklore, which is
abundant, confident, and frequently a decade out of date.

Requiring a number also changes what happens in review. "I think this is faster"
cannot be answered except by opinion; "this is 1.4× faster on a 100k-element
input in release" can be reproduced, disputed, or found to be measuring the
wrong thing.

## Consequences

### Positive

- Optimisation arguments become checkable, and the wrong ones can be dropped.
- Profiling routinely finds hot spots nobody predicted, which is where the real
  gains are.
- Recorded measurements let a later reader tell whether an optimisation is still
  earning its complexity.

### Negative

- Good measurement is genuinely expensive: representative workloads, quiet
  machines, repeated runs, and tools that have to be learned. The cost is often
  larger than the change being evaluated.
- A bad measurement is worse than none, because it carries the authority of a
  number. Dead-code elimination, warm caches and unrepresentative inputs all
  produce plausible wrong answers.
- Strictly applied, the rule forbids ordinary design reasoning about cost —
  saying an extra indirection is likely to hurt — which is often correct and
  cannot always wait for an experiment.

### Neutral

- No specific benchmarking or profiling tooling is established here; this record
  states the obligation, not the mechanism.
- Order-of-magnitude cost intuition, of the sort
  [0244](0244-per-7-design-to-enable-optimization.md) asks for, is a design aid
  and not a performance claim.

## Related

- [0238](0238-per-1-dont-optimize-without-reason.md) — the reason to optimise is
  established the same way.
- [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) —
  what a profiler is for.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — profilers and
  benchmark harnesses are the supporting tools that rule asks for.
- [0241](0241-per-4-dont-assume-that-complicated-code-is-necessarily-faster-than-simple-code.md)
  — a specific myth this rule exists to dispel.
