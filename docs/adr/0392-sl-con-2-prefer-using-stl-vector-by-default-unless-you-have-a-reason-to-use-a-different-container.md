# 0392. SL.con.2: Prefer using STL vector by default unless you have a reason to use a different container

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

`vector` and `array` are the only standard containers that offer the fastest
general-purpose access, which is random access and vectorisation-friendly; the
fastest default access pattern, since begin-to-end or end-to-begin traversal is
prefetcher-friendly; and the lowest space overhead, since a contiguous layout
has zero per-element overhead and is cache-friendly.

Usually you need to add and remove elements, so use `vector` by default; if you
do not need to modify the container's size, use `array`. Even when other
containers seem more suited — `map` for O(log N) lookup, or a list for efficient
insertion in the middle — a `vector` will usually still perform better for
containers up to a few kilobytes in size.

`string` should not be used as a container of individual characters. A string is
a textual string; if you want a container of characters, use
`vector<char_type>` or `array<char_type>` instead.

If you have a good reason to use another container, use that instead. If
`vector` suits your needs but you do not need the container to be variable size,
use `array`. If you want a dictionary-style lookup container that guarantees
O(K) or O(log N) lookups, the container will be larger than a few kilobytes, and
you perform frequent inserts so that the overhead of maintaining a sorted
`vector` is infeasible, then use an `unordered_map` or `map`.

To initialise a `vector` with a number of elements, use `()`-initialisation. To
initialise it with a list of elements, use `{}`-initialisation:

```cpp
vector<int> v1(20);  // v1 has 20 elements with the value 0 (vector<int>{})
vector<int> v2 {20}; // v2 has 1 element with the value 20
```

## Decision

Make `std::vector` the container you reach for unless a specific requirement
says otherwise.

- Start from `vector`; move to `array` when the size is fixed and known at
  compile time.
- Justify any other container by a stated requirement — a size well beyond a
  few kilobytes with frequent insertion, a lookup complexity guarantee, or
  reference and iterator stability — not by the shape of the operations alone.
- Do not use `std::string` as a general container of characters; use
  `vector<char>` or `array<char, N>` for that.
- Use `()` to give a `vector` a size and `{}` to give it contents, and read
  every `vector` construction with that distinction in mind.

## Rationale

The argument that decides this is memory locality, not asymptotic complexity.
Node-based containers pay a pointer and an allocation per element, and their
traversal defeats the prefetcher; at small sizes that constant factor swamps the
better complexity class. The crossover is higher than intuition suggests, which
is why "`map` for lookup, `list` for insertion" is usually the wrong reflex.

Defaulting also has a value independent of performance: a codebase where the
sequence container is always `vector` is one where the choice of anything else
is visible and carries information.

## Consequences

### Positive

- Contiguous storage gives the best cache behaviour and the lowest per-element
  overhead by default.
- A single default container makes any deviation a deliberate, reviewable
  signal.
- `vector` interoperates directly with `span`, ranges and C APIs through
  `.data()`.

### Negative

- `vector` invalidates references and iterators on reallocation, and erasing
  from the middle is linear; where those matter the default is simply wrong,
  and the rule's escape hatch is a judgement call with no threshold given.
- "A few kilobytes" is offered as the crossover point without a measurement
  method, so the guidance is easy to cite in both directions.
- The `v1(20)` versus `v2{20}` distinction is a genuine trap that this rule
  documents rather than removes, and it pulls against the general preference for
  brace initialisation in
  [0186](0186-es-23-prefer-the-initializer-syntax.md).
- Reserving capacity to avoid repeated reallocation is extra code that a
  node-based container would not have needed.

### Neutral

- `array` and `vector` are treated as one choice with two storage strategies,
  not as competing containers.
- The rule is about the default, and explicitly permits any container for which
  a reason exists.

## Enforcement

- Flag a `vector` whose size never changes after construction, for example
  because it is `const` or because no non-`const` member function is ever called
  on it. The fix is to use an `array` instead.

## Related

- [0391](0391-sl-con-1-prefer-using-stl-array-or-vector-instead-of-a-c-array.md)
  — the same two containers, chosen over the built-in array.
- [0186](0186-es-23-prefer-the-initializer-syntax.md) — the initialisation
  preference this rule carves an exception out of.
- [0251](0251-per-16-use-compact-data-structures.md) — the locality argument
  that makes contiguity the default.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md)
  — required before deviating on performance grounds.
