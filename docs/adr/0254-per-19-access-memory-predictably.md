# 0254. Per.19: Access memory predictably

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Performance is very sensitive to cache performance, and cache algorithms favour
simple, usually linear, access to adjacent data. The upstream example is two
loops over the same array, differing only in which index is on the outside:

```cpp
int matrix[rows][cols];

// bad
for (int c = 0; c < cols; ++c)
    for (int r = 0; r < rows; ++r)
        sum += matrix[r][c];

// good
for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c)
        sum += matrix[r][c];
```

Both loops read every element exactly once, do the same additions, and produce
the same answer. In C and C++ a two-dimensional array is stored row by row, so
the second loop walks memory from beginning to end: each cache line fetched
supplies the next several iterations, and the hardware prefetcher, recognising
a constant stride, has the following lines on their way before they are asked
for. The first loop steps by a whole row each time, so every access lands on a
different line; if the rows are long enough it also lands on a different page,
and once the working set exceeds the cache nothing fetched is still resident by
the time the next column comes round. The measured difference on a large matrix
is routinely an order of magnitude, from a transposition of two lines of source.

The general property is predictability rather than mere adjacency. The
prefetcher can follow a constant stride, forwards or backwards; it cannot follow
a chain of pointers, an index computed from the data, or a hash. That is why a
linked traversal is slow even when the nodes happen to have been allocated
consecutively — the address of the next node is not known until the current one
has been loaded, so nothing can be issued in advance and every miss is paid in
full.

## Decision

Traverse data in the order it is laid out.

- Iterate a multi-dimensional array with the last index varying fastest, so the
  innermost loop walks contiguous memory.
- Make the innermost loop the one that strides by one element. Where the loops
  are the wrong way round for the layout, either interchange them or change the
  layout.
- Prefer one pass that touches each element once to several passes over the same
  data, and prefer a sequential pass over the whole structure to a scattered one
  over part of it.
- Avoid pointer chasing on a hot path; a chain of dependent loads defeats
  prefetching entirely. This is the layout half of
  [0248](0248-per-13-eliminate-redundant-indirections.md).
- Where elements are visited in an order determined by the data — an index list,
  a hash, a sorted key — consider reordering the data itself into visit order
  once, and then sweeping it.
- Keep fields used together in the same pass adjacent, and split a hot structure
  from its cold fields if the sweep only needs some of them.
- Confirm with a measurement. Access patterns interact with cache size, and the
  crossover between "fits" and "does not" is where the behaviour changes.

## Rationale

This is the cheapest large win in the section. The bad loop and the good loop
are the same algorithm with the same complexity and the same instruction count;
the difference is entirely in what the hardware was able to anticipate, and the
fix costs nothing but attention.

It also shows what "dominated by memory access times" means concretely. Two
programs that are identical by every measure a complexity analysis can see
differ by an order of magnitude, which is why the layout and the traversal
order have to be reasoned about explicitly rather than left to follow from the
algorithm.

## Consequences

### Positive

- The prefetcher can run ahead of a strided sweep, so most accesses are hits.
- Each cache line fetched is used completely rather than for one element.
- The improvement is usually free: a loop interchange changes no logic.

### Negative

- The natural expression of an algorithm is sometimes column-major, and forcing
  it into row order means transposing either the loop or the data, which costs
  clarity or a copy.
- Reordering data into access order is only worth it when the data is swept
  repeatedly; done once per traversal it costs more than it saves.
- Optimising for one access pattern penalises another, and a structure with two
  important patterns cannot satisfy both.
- The right answer depends on cache sizes and prefetcher behaviour that differ
  between machines, so a layout tuned on one target is not necessarily right on
  another.

### Neutral

- The rule concerns the order of access; the size and shape of what is accessed
  is [0251](0251-per-16-use-compact-data-structures.md) and
  [0253](0253-per-18-space-is-time.md).
- Row-major storage is a property of C and C++ arrays; other languages and some
  matrix libraries lay out the other way, and the rule follows the layout rather
  than the index name.

## Related

- [0251](0251-per-16-use-compact-data-structures.md) — a compact layout only
  pays if it is traversed in order.
- [0253](0253-per-18-space-is-time.md) — the same premise about memory access
  times.
- [0248](0248-per-13-eliminate-redundant-indirections.md) — pointer chasing is
  the least predictable access pattern there is.
- [0241](0241-per-4-dont-assume-that-complicated-code-is-necessarily-faster-than-simple-code.md)
  — the simple loop is the one the hardware and the optimiser both handle best.
