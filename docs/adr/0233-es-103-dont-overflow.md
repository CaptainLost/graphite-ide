# 0233. ES.103: Don't overflow

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Overflow usually makes a numeric algorithm meaningless, and incrementing a value
beyond a maximum can lead to memory corruption and undefined behaviour. The
guidelines treat running off the end of an array and running past the top of an
integer type as the same rule, because both are the case of a value exceeding
the range it is allowed to take.

```cpp
int a[10];
a[10] = 7;   // bad, array bounds overflow

for (int n = 0; n <= 10; ++n)
    a[n] = 9;   // bad, array bounds overflow
```

```cpp
int n = numeric_limits<int>::max();
int m = n + 1;   // bad, numeric overflow
```

```cpp
int area(int h, int w) { return h * w; }

auto a = area(10'000'000, 100'000'000);   // bad, numeric overflow
```

The `area` case is the instructive one: neither argument is unreasonable, the
function is obviously correct, and the product simply does not fit. Use unsigned
types if you really want modulo arithmetic. For critical applications that can
afford some overhead, use a range-checked integer or floating-point type.

## Decision

Ensure every arithmetic and indexing operation stays inside its range.

- Write loop conditions with `<` against the size, not `<=`, and derive the
  bound from the container rather than repeating a literal.
- Use `span`, range-`for` or an algorithm so the bound cannot be stated
  incorrectly.
- Choose a type wide enough for the domain, and where a product or sum can
  exceed the operand type, widen before computing, not after.
- Where inputs come from outside the function, state the admissible range as a
  precondition with `Expects` and check it.
- Use a range-checked integer type where the computation is critical and the
  overhead is affordable.
- Use unsigned types only where wraparound is the intended semantics, and say
  so.

## Rationale

Signed overflow is undefined behaviour, so the consequence is not a wrong number
but an optimiser entitled to assume the operation never happens — which is how
an overflow in one expression removes a bounds check in another. Treating
overflow as merely inaccurate underestimates it by a wide margin.

The reason the rule is stated as bluntly as "don't" is that the language offers
no help: there is no diagnostic, and the guidelines themselves record no
enforcement. What makes it actionable is the alternatives — a wide enough type,
a checked type, a precondition, or an abstraction that removes the arithmetic —
which is [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
applied to arithmetic.

## Consequences

### Positive

- Numeric results remain meaningful across the whole input domain rather than a
  subset nobody wrote down.
- Removes the buffer overruns that come from a loop bound stated as `<=`.
- Forcing the admissible range to be stated as a precondition documents what the
  function actually accepts.

### Negative

- The rule has no enforcement. The guidelines give none, and detecting overflow
  statically in general is undecidable, so compliance rests entirely on the
  author's arithmetic.
- Range-checked integer types cost performance and change the type of every
  value they touch, so they are adopted at module scale or not at all.
- Widening to avoid an intermediate overflow makes expressions longer and adds
  casts to code that looked correct, and it is easy to widen after the
  multiplication rather than before, which achieves nothing.
- A precondition can only reject values the caller passed; it cannot bound a
  product accumulated across many iterations.

### Neutral

- Deliberate modulo arithmetic on unsigned types is not overflow and is
  explicitly permitted; see
  [0231](0231-es-101-use-unsigned-types-for-bit-manipulation.md).
- Floating-point overflow to infinity is a different failure with different
  rules and is not what this record addresses.

## Related

- [0234](0234-es-104-dont-underflow.md) — the same rule at the other end of the
  range.
- [0232](0232-es-102-use-signed-types-for-arithmetic.md) — signed types are what
  make overflow diagnosable by a sanitizer.
- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — how the
  admissible range is stated.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the checked type and the precondition are the run-time check this rule
  needs.
