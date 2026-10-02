# 0239. Per.2: Don't optimize prematurely

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Elaborately optimised code is usually larger and harder to change than
unoptimised code. That is the whole of the upstream reason, and it is an
observation about maintenance rather than about speed: the optimised version may
well be faster, but it is certainly bigger, and its size is paid every time
someone has to modify it.

Prematurely means before it is known which code matters. Early in a program's
life that is almost everything, because the shape of the program is still
changing and the parts that will dominate its running time have not yet been
identified — often they turn out to be parts that did not exist yet when the
optimising was done. Work spent tightening code that is later deleted, moved
behind a cache, or called once per session is work spent making the rest of the
program harder to write.

The rule is frequently quoted as if it were hostility to performance. It is not.
It is a statement about ordering: optimise when there is something to optimise
against, and not before. Considering what makes a design amenable to later
improvement is never premature, which is the subject of a separate rule.

## Decision

Do not optimise before the program's cost structure is known.

- Get the program correct, then let it run under a realistic workload, then
  decide where effort goes.
- Do not hand-specialise, cache, pool, or inline by hand on the strength of a
  guess about what will be hot.
- Prefer the change that is easy to reverse. An optimisation that survives the
  rewrite of the surrounding code is rare; one that obstructs the rewrite is
  common.
- Keep design-level decisions — interface shape, data layout, ownership — out of
  this prohibition. Those are covered by
  [0244](0244-per-7-design-to-enable-optimization.md) and are cheapest before the
  code exists, not after.
- When an early optimisation is nonetheless judged necessary, treat it as a
  decision to be justified, not as a default.

## Rationale

The asymmetry is what makes the rule worth stating. Optimising too late costs
some rework in a place already identified as important; optimising too early
costs complexity spread across code whose importance was never established, and
which resists every later change.

A program that is simple is also a program whose hot spots can be found. Once
the code is elaborate everywhere, profiling reports where the time goes but no
longer suggests what to do about it, because every candidate change is entangled
with a hand-optimisation whose reason has been forgotten.

## Consequences

### Positive

- Code stays small and changeable through the period when it changes most.
- Optimisation effort lands after the evidence, so less of it is wasted.
- Reverting a design decision does not mean unpicking a hand-optimisation that
  was built on top of it.

### Negative

- Deferred optimisation can become structural debt: some costs, once the data
  layout or the interface has hardened, can no longer be removed without a
  rewrite. This rule and
  [0244](0244-per-7-design-to-enable-optimization.md) genuinely pull in opposite
  directions and the boundary between them is a matter of experience.
- "Premature" has no test. In practice the label is applied after the fact, which
  makes it a poor guide at the moment the decision is taken.
- Waiting for a realistic workload assumes one exists. For code written before
  its users, the rule offers nothing.

### Neutral

- The rule does not say optimisation is unimportant, only that it is ordered
  after correctness and measurement.
- It says nothing about avoiding waste, which is not optimisation and is never
  premature.

## Related

- [0238](0238-per-1-dont-optimize-without-reason.md) — the same caution, applied
  to motive rather than timing.
- [0244](0244-per-7-design-to-enable-optimization.md) — the complement, and the
  limit of this rule: enabling later optimisation is never premature.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  what has to happen before optimising stops being premature.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — eliminating waste is a
  separate obligation that applies from the first line.
