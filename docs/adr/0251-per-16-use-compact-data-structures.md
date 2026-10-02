# 0251. Per.16: Use compact data structures

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Performance is typically dominated by memory access times. That is the whole of
the upstream reason, and it is the premise the rest of this group of rules rests
on.

The claim is about the shape of modern hardware. An arithmetic instruction costs
a fraction of a nanosecond; a hit in the first-level cache costs a few cycles; a
miss all the way to main memory costs on the order of a hundred nanoseconds,
during which the processor may have nothing to do. Data is moved in cache lines,
typically 64 bytes, so what is fetched is not the value asked for but the
neighbourhood it sits in. A structure that packs useful data densely gets the
neighbours for free; one that spreads it out pays a full miss for each item and
discards most of what it fetched.

This is why the guidelines' advice on data structures is so blunt elsewhere in
the section: use compact data such as `std::vector` by default and access it in
a systematic fashion, and if a linked structure seems necessary, craft the
interface so that users do not see it. A `std::list` and a `std::vector` have
the same asymptotic cost for traversal and wildly different real costs, because
one is a stream of adjacent bytes and the other is a chain of dependent loads
into unrelated pages. The same comparison holds between `std::map` and a sorted
`std::vector`, and between a hash table with separate chaining and one with open
addressing.

Compactness is also a property of individual types, not just containers.
Padding inserted for alignment, a `bool` occupying a word, an owning pointer to
an object that would have fitted inline, an enumeration stored as an `int` when
a byte would do — each enlarges every element and reduces how many fit in a
line.

## Decision

Choose data structures for their memory behaviour, not only their asymptotic
complexity.

- Default to `std::vector` and other contiguous storage. Choose a node-based
  container when its properties — reference stability, ordered iteration, cheap
  splicing — are actually needed, not by habit.
- Where a linked or node-based structure is required, keep it out of the
  interface, so it can be replaced later without changing callers.
- Store elements by value rather than as owning pointers where the element type
  is not polymorphic; see
  [0248](0248-per-13-eliminate-redundant-indirections.md).
- Keep element types small: order members so padding is not inserted
  gratuitously, use a sized enumeration or a narrow integer where the range
  allows, and do not carry fields that are derivable.
- Separate hot fields from cold ones where a type is traversed in bulk. Fields
  read on every pass and fields read rarely do not belong on the same cache line
  if the structure is large.
- Reach for a sorted `vector` with binary search in place of a `map` where the
  data is built once and then queried.
- Confirm the change with a measurement, per
  [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md);
  compactness is a strong prior, not a guarantee.

## Rationale

The reason this rule outranks most micro-optimisation is that the gap it
addresses is two orders of magnitude wide, while the gap between a good and a
bad instruction sequence is a small factor. No amount of care in the code
recovers a cache miss that a better layout would have avoided.

It also explains why complexity analysis so often mispredicts. Big-O counts
operations and treats every memory access as equal, which stopped being true
long ago; the constant factor hidden inside "one access" is where the time
actually goes.

## Consequences

### Positive

- Traversal becomes a predictable stream the prefetcher can stay ahead of.
- More useful data arrives per cache line, so the same work touches less memory.
- Contiguous containers also allocate less, which compounds with
  [0249](0249-per-14-minimize-the-number-of-allocations-and-deallocations.md).

### Negative

- Contiguous storage invalidates references and iterators on growth, where a
  node-based container does not. Code that holds a pointer into a container has
  to be restructured, often around indices, which is less safe.
- Insertion and erasure in the middle of a `vector` move elements; for large
  elements or large containers that can be worse than the layout is better.
- Ordering members for packing conflicts with grouping them by meaning, and
  splitting hot from cold fields splits an object that conceptually is one
  thing. Both are readability costs paid for a speed gain that is often
  irrelevant, as [0015](0015-p-9-do-not-waste-time-or-space.md) already notes.
- The rule is easy to over-apply. Most data structures in a program are small,
  touched rarely, and would be better left in whatever form reads most clearly.

### Neutral

- The upstream rule is one sentence; the specific practices above are a reading
  of it, not transcription.
- Where compactness genuinely trades against something else, that trade is the
  subject of [0253](0253-per-18-space-is-time.md), which does not always resolve
  in favour of smaller.

## Related

- [0253](0253-per-18-space-is-time.md) — the same premise stated as a slogan.
- [0254](0254-per-19-access-memory-predictably.md) — compact layout only pays if
  it is traversed in order.
- [0252](0252-per-17-declare-the-most-used-member-of-a-time-critical-struct-first.md)
  — the member-level corollary.
- [0244](0244-per-7-design-to-enable-optimization.md) — where "use compact data
  by default, and hide any linked structure" is stated as a design habit.
