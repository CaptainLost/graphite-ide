# 0253. Per.18: Space is time

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Performance is typically dominated by memory access times. Upstream gives this
rule the same one-line reason it gives
[0251](0251-per-16-use-compact-data-structures.md), and no example; the title is
the argument.

The slogan inverts an assumption old enough to be reflexive: that space and time
are traded against each other, so a program can be made faster by spending
memory on it. On hardware where memory is much slower than arithmetic, the trade
frequently runs the other way. Halving the size of an element doubles how many
fit in a cache line, halves the number of lines a sweep must fetch, and halves
the pressure on every level of cache — so the smaller program is the faster one,
and no cleverness was involved.

The clearest cases are the ones where the space was bought deliberately. A
lookup table added to avoid a computation is faster only while it stays in
cache; once it is large enough to be evicted between uses, each lookup costs a
miss, and recomputing the value from registers would have been quicker. A cached
derived field enlarges every instance to save work that may be a handful of
instructions. A memoisation table that outgrows the cache is slower than the
function it replaced.

The slogan is not a rule that smaller is always faster, and it should not be
read as one. Reserving capacity to avoid reallocation spends space to save time
and usually wins. A hash table deliberately kept sparse to reduce collisions
spends space to save time and wins. What the slogan says is that the direction
of the trade is not known in advance, so it must be established rather than
assumed — which is why it sits in a section that also insists on measurement.

## Decision

Treat memory footprint as a determinant of speed, not as something traded away
for it.

- Do not assume that spending memory buys time. When adding a table, a cache, or
  a precomputed field, measure the result against not having it.
- Prefer the smaller representation where it is otherwise equal: a narrower
  integer, a sized enumeration, `std::byte` in place of a word, a value in place
  of an owning pointer.
- Do not store what can be cheaply recomputed from data already in registers or
  in the same cache line. A few arithmetic instructions are cheaper than a miss.
- Size a lookup table against the cache it must live in. A table larger than the
  level it is expected to occupy is a miss generator.
- Keep the space bought deliberately — reserved capacity, preallocated buffers,
  deliberate sparseness — and justify it in the same terms: what it saves, and
  measured how.

## Rationale

The argument that makes the slogan worth stating is that the exchange rate has
changed by two orders of magnitude and intuition has not kept up. Programmers
still reach for a table to avoid a multiply, on hardware where the multiply
costs a cycle and the table lookup can cost a hundred.

It also sharpens [0251](0251-per-16-use-compact-data-structures.md) from advice
into a reason. Compact data structures are not preferred because tidy layouts
are pleasing; they are preferred because on this hardware size is one of the
main things that determines how long the program takes.

## Consequences

### Positive

- Smaller data means fewer cache lines fetched and less pressure at every level,
  so the same work touches less memory.
- Speculative space-for-time trades are made suspect and have to be measured,
  which removes many that never paid.
- Space and time stop being separate budgets to reason about in most cases.

### Negative

- Taken literally, the slogan is false in an important set of cases —
  `reserve`, buffer reuse, preallocation ahead of a deadline — and those are
  precisely the cases the neighbouring rules
  [0249](0249-per-14-minimize-the-number-of-allocations-and-deallocations.md)
  and [0250](0250-per-15-do-not-allocate-on-a-critical-branch.md) point towards.
  A slogan that pulls against its neighbours has to be applied with judgement it
  does not supply.
- Recomputing instead of storing costs code, and the recomputation must be kept
  consistent with whatever it is derived from — a correctness risk taken for a
  speed gain.
- Shrinking representations to save bytes introduces conversions, narrower
  ranges, and overflow risk in code that previously had none.
- Upstream reduces the rule to its title and a single sentence of reason, with
  no example, so any concrete guidance — this record included — is
  interpretation.

### Neutral

- The rule shares its premise with
  [0251](0251-per-16-use-compact-data-structures.md); the two are one argument
  stated twice.
- It says nothing about which of two representations of equal size to prefer.

## Related

- [0251](0251-per-16-use-compact-data-structures.md) — the same premise as a
  practice.
- [0254](0254-per-19-access-memory-predictably.md) — the third rule from that
  premise: size, order, and the pattern of access.
- [0249](0249-per-14-minimize-the-number-of-allocations-and-deallocations.md) —
  a rule that generally pulls the other way, towards holding memory to save
  time.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the philosophy rule that
  treats the two as one obligation.
