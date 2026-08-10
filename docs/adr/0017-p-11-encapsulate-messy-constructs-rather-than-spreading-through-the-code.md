# 0017. P.11: Encapsulate messy constructs, rather than spreading through the code

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Messy code is more likely to hide bugs and harder to write. A good interface is
easier and safer to use. Messy, low-level code breeds more such code.

The upstream example is a hand-grown dynamic array:

```cpp
int sz = 100;
int* p = (int*) malloc(sizeof(int) * sz);
int count = 0;
// ...
for (;;) {
    // ... read an int into x, exit loop if end of file is reached ...
    // ... check that x is valid ...
    if (count == sz)
        p = (int*) realloc(p, sizeof(int) * sz * 2);
    p[count++] = x;
    // ...
}
```

This is low-level, verbose, and error-prone — the test for memory exhaustion has
been "forgotten", and the `realloc` never updates `sz`. Using `vector` instead:

```cpp
vector<int> v;
v.reserve(100);
// ...
for (int x; cin >> x; ) {
    // ... check that x is valid ...
    v.push_back(x);
}
```

The standard library and the GSL are examples of this philosophy. Rather than
messing with the arrays, unions, casts, tricky lifetime issues and `gsl::owner`
that are needed to implement key abstractions such as `vector`, `span`,
`lock_guard` and `future`, one uses libraries designed and implemented by people
with more time and expertise than is usually available. Similarly, one can and
should design and implement more specialised libraries, rather than leaving the
users — often oneself — with the challenge of repeatedly getting low-level code
right.

## Decision

Encapsulate messy constructs behind an interface instead of spreading them
through the code.

- Where an existing library abstraction covers the mess — a container, a smart
  pointer, a lock guard — use it rather than reimplementing it.
- Where none does, write the abstraction: put the casts, the raw allocation, the
  platform call and the lifetime trickery inside one type or one translation
  unit, and give the rest of the code a safe interface.
- Do not let a messy construct appear in calling code because it is "only one
  line here". One line, repeated, is the failure mode this rule exists to stop.
- Keep the messy region small enough to be reviewed carefully and tested
  directly.

## Rationale

The argument is about where the difficulty is allowed to live, not whether it
exists. Low-level code is sometimes unavoidable; what is avoidable is having
twenty copies of it, each of which must be correct, and each of which is a place
a later reader can copy a twenty-first from.

Concentrating it also concentrates the review effort and the tests: one hard
type that is tested properly is a better bet than a hundred call sites that are
each individually plausible.

## Consequences

### Positive

- Unsafe constructs live in few, known places, so review and testing can be
  aimed at them.
- Calling code reads in terms of what it means rather than how it is done.
- A bug in the messy part is fixed once, not at every copy.

### Negative

- An abstraction is a design commitment; a badly chosen interface is harder to
  live with than the inline mess would have been, and it will be used by code
  that then depends on it.
- Encapsulation can hide cost as well as complexity, which pulls against
  [0015](0015-p-9-do-not-waste-time-or-space.md).
- Writing a specialised library is a real investment, and one that can outgrow
  the problem it was meant to solve.

### Neutral

- This record does not say when to write a new abstraction versus adopting an
  existing library; that trade-off is
  [0019](0019-p-13-use-support-libraries-as-appropriate.md).
- It is a variant of the subset-of-superset principle underlying these
  guidelines: use a safer subset of the language, extended by libraries.

## Enforcement

- Look for "messy code" such as complex pointer manipulation and casting
  outside the implementation of abstractions.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — localising a platform extension
  is this rule with a specific mess in mind.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — prefer someone
  else's abstraction where one exists.
