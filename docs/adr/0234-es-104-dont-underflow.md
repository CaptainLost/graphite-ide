# 0234. ES.104: Don't underflow

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Decrementing a value beyond a minimum can lead to memory corruption and
undefined behaviour. As with overflow, the guidelines treat indexing below the
start of an array and decrementing past a type's minimum as one rule.

```cpp
int a[10];
a[-2] = 7;   // bad

int n = 101;
while (n--)
    a[n - 1] = 9;   // bad (twice)
```

The second example is bad twice over. The array has ten elements and `n` starts
at 101, so the loop writes far past both ends; and because `n--` yields the
value before the decrement, the last iteration runs with `n == 0` and indexes
`a[-1]`. Both mistakes are in two lines that look like an ordinary countdown.

Use unsigned types if you really want modulo arithmetic.

## Decision

Ensure every decrement and every subtraction stays inside its range.

- Derive countdown bounds from the container, and check the index that is
  actually used rather than the loop variable it is computed from.
- Be explicit about whether `n--` or `--n` is intended when the value is used in
  the same expression; prefer separating the decrement from the use.
- Do not subtract from an unsigned value that may be smaller than the
  subtrahend; `v.size() - 1` on an empty container is the largest `size_t`, not
  `-1`.
- Compute differences in a signed type wide enough for the negative result, per
  [0232](0232-es-102-use-signed-types-for-arithmetic.md).
- State a lower bound as a precondition where the value comes from a caller, and
  check it.
- Prefer reverse iterators, `rbegin`/`rend` or a reversed range to a hand-rolled
  descending index loop.

## Rationale

Underflow is the more dangerous half of the pair, for two reasons. It usually
arrives through subtraction rather than accumulation, so it happens in a single
step from ordinary-looking values rather than after many iterations; and on
unsigned types it is defined behaviour, so it produces an enormous positive
number with no undefined behaviour for a sanitizer to catch. A descending loop
written with an unsigned index does not underflow — it wraps, and then indexes
with a huge value, which is exactly the bug in
[0237](0237-es-107-dont-use-unsigned-for-subscripts-prefer-gsl-index.md).

Descending loops are also where the off-by-one lives: the ascending form has one
conventional shape everyone recognises, while the descending form has several,
and the guidelines' own example gets two things wrong at once.

## Consequences

### Positive

- Removes the indexing-below-zero bugs that descending loops produce.
- Makes `size() - 1` on a possibly-empty container a case the author must
  consider rather than one that silently produces a huge index.
- Reverse iteration expressed with reverse iterators has no index to get wrong.

### Negative

- The rule has no enforcement in the guidelines, and no compiler diagnoses
  unsigned wraparound because it is well-defined; correctness rests on the
  author.
- Guarding against underflow means checking before subtracting, which adds a
  branch to arithmetic that reads as trivial and is usually right.
- Signed indices, which make underflow visible, require converting away from the
  standard library's unsigned sizes at every boundary — the cost catalogued in
  [0230](0230-es-100-dont-mix-signed-and-unsigned-arithmetic.md).
- Reverse iterators avoid the arithmetic but read less directly than an index
  loop for anyone not fluent in them.

### Neutral

- Deliberate modulo arithmetic on unsigned types is explicitly permitted and is
  not what this rule prohibits.
- Floating-point underflow to a denormal or to zero is a different phenomenon
  and is not covered here.

## Related

- [0233](0233-es-103-dont-overflow.md) — the same rule at the other end of the
  range.
- [0237](0237-es-107-dont-use-unsigned-for-subscripts-prefer-gsl-index.md) — the
  descending loop with an unsigned index, in full.
- [0232](0232-es-102-use-signed-types-for-arithmetic.md) — why the difference
  should be computed in a signed type.
- [0235](0235-es-105-dont-divide-by-integer-zero.md) — the third member of the
  same family of arithmetic preconditions.
