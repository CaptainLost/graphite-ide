# 0242. Per.5: Don't assume that low-level code is necessarily faster than high-level code

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Low-level code sometimes inhibits optimisations, and optimisers sometimes do
marvels with high-level code. Upstream gives this rule a two-sentence reason and
no example; the section's other rules supply the evidence.

The belief being corrected is that abstraction is a layer of cost sitting on top
of the machine, so removing it must expose the machine and go faster. In C++
that is usually backwards. The abstractions the standard library provides are
designed for zero overhead — `std::sort` beats `qsort` rather than losing to it,
because it knows the element type and can inline the comparison, while `qsort`
must call through a function pointer over `void*`. A range-`for` over a
`vector` carries no more work than the equivalent index loop and gives the
compiler a bound it can prove. `std::unique_ptr` compiles to the raw pointer it
wraps.

What actually happens when code is written at a lower level is that information
is thrown away. Types become bytes, containers become pointers and counts,
calls become indirect. The optimiser can only transform what it can prove, and
each of those steps removes a proof it was relying on. The result is code that
looks closer to the machine and gives the machine less to work with.

Elsewhere in this section the guidelines put it directly: a need for efficiency
does not imply a need for low-level code, and high-level code does not imply
slow or bloated.

## Decision

Do not descend a level of abstraction for speed without evidence that the
descent is what buys it.

- Reach for a standard-library algorithm or container before a hand-written loop
  over raw memory; it is at least as fast and carries more information to the
  optimiser.
- Keep types specific. Replacing a typed interface with `void*`, `char*` or a
  byte count removes exactly the facts the optimiser needs.
- Prefer a call the compiler can see and inline to one it cannot. A function
  pointer or a virtual call in an inner loop is an optimisation barrier, whatever
  level of abstraction it is written at.
- When a low-level rewrite is proposed, measure the high-level version first,
  with optimisation enabled, and compare against it.
- Where a low-level version is genuinely needed, confine it behind a typed
  interface rather than letting it spread, as
  [0039](0039-i-30-encapsulate-rule-violations.md) requires.

## Rationale

Abstraction in C++ is mostly a compile-time phenomenon: it exists to inform the
compiler, and it is erased before the code runs. Removing it therefore removes
information without removing work, which is the opposite of the intended trade.

The `qsort` case is the concrete demonstration and it is not marginal. The
low-level interface forces an indirect call per comparison and hides the element
type; the template beats it not by being cleverer but by still knowing what it
is sorting.

## Consequences

### Positive

- Library abstractions are used as intended, and are usually the faster option
  as well as the safer one.
- Type information survives to the optimiser, where it does the most good.
- Less raw-memory code means fewer aliasing, alignment and lifetime bugs.

### Negative

- Abstraction has a real compile-time cost: heavy template use slows builds and
  produces worse debug-build performance, which matters for a program run
  constantly under a debugger during development.
- The rule is not universal. Some hardware-facing work — device interaction,
  fixed layouts, alignment-sensitive code — genuinely requires the low-level
  form, and the rule risks discouraging it.
- Judging whether an abstraction is zero-overhead requires knowing how it is
  implemented, which is exactly the knowledge the abstraction was meant to spare
  the caller.

### Neutral

- The rule addresses assumptions, not permissions; a measured low-level version
  remains legitimate.
- Nothing here says how to choose between two abstractions of equal level.

## Related

- [0241](0241-per-4-dont-assume-that-complicated-code-is-necessarily-faster-than-simple-code.md)
  — the same error framed as complexity rather than level.
- [0245](0245-per-10-rely-on-the-static-type-system.md) — the mechanism: weak
  types make the optimiser's job harder.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — a good library is
  usually faster than the hand-rolled replacement.
- [0244](0244-per-7-design-to-enable-optimization.md) — `qsort` against `sort`,
  worked through in full.
