# 0238. Per.1: Don't optimize without reason

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

The performance section opens with a warning about itself. Its rules are for
people who need high performance or low latency — code that must use as little
time and as few resources as possible to finish in a predictably short time —
and they are more restrictive and intrusive than most applications need.
Achieving the goals of low latency requires extra work, so applying these rules
naïvely to general code buys their cost without their benefit.

This first rule is the gate on all the others. If there is no need for
optimisation, the main result of the effort will be more errors and higher
maintenance costs. The upstream note names the usual motive candidly: some
people optimise out of habit, or because it is fun.

Optimisation is not free even when it succeeds. It replaces code whose
correctness is obvious with code whose correctness is an argument, and the
argument has to be re-made by everyone who touches the code afterwards. That
price is worth paying when something is actually too slow. It is pure loss when
nothing is.

## Decision

Optimise only in response to a stated need.

- Before changing code for speed, name the requirement it fails: a frame budget,
  a latency bound, a memory ceiling, a startup time, a user-visible complaint.
  "It could be faster" is not a requirement.
- Write the clear version first and leave it until something says it is not good
  enough.
- When an optimisation is made, record in a comment or a commit message what it
  was measured against, so a later reader can tell whether the reason still
  holds.
- Treat "this is the performance-conscious way to write it" as a claim needing
  support, not as a default style.
- Distinguish this rule from avoiding gratuitous waste. Not allocating a buffer
  you never needed is not optimisation; it is
  [0015](0015-p-9-do-not-waste-time-or-space.md), and it applies always.

## Rationale

The cost of an unmotivated optimisation is not paid once. Complicated code is
read more often than it is written, and every subsequent change has to preserve
a property nobody can state, because nobody wrote down what the code was
supposed to be faster than.

Requiring a reason also makes the optimisation falsifiable. A change made
against a named budget can later be shown to be unnecessary and removed; a
change made out of habit can never be removed, because there is nothing to
disprove.

## Consequences

### Positive

- Effort goes where it changes something, rather than where it is enjoyable.
- Code stays in its simplest correct form by default, which is the form that is
  cheapest to change.
- An optimisation with a recorded reason can be revisited when the reason
  expires; one without cannot.

### Negative

- "No reason" is a judgement, and the rule gives no threshold, so it can be used
  to block work that would in fact have been worthwhile.
- Some performance decisions are structural and cheap only before the code
  exists; deferring them until a reason appears can mean deferring them past the
  point where they are affordable. [0244](0244-per-7-design-to-enable-optimization.md)
  exists precisely because of this hole.
- Waiting for a need means the need usually arrives as a complaint, when time is
  short and the fix has to be found in code already written.

### Neutral

- The rule says nothing about how much optimisation is enough once a reason
  exists.
- Whether a given need is real is decided by measurement, which is a separate
  rule.

## Related

- [0239](0239-per-2-dont-optimize-prematurely.md) — the same caution aimed at
  timing rather than at motive.
- [0240](0240-per-3-dont-optimize-something-thats-not-performance-critical.md) —
  even with a reason, the reason has to point at the right code.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  how a claimed need is shown to be real.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the philosophy rule this
  section serves, and the one that applies without needing a reason.
