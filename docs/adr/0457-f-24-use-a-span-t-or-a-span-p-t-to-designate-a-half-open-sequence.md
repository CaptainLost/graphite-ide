# 0457. F.24: Use a span<T> or a span_p<T> to designate a half-open sequence

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Informal, non-explicit ranges are a source of errors. Where a function needs a
sequence rather than a single object, the sequence should be one argument that
carries its own extent:

```cpp
X* find(span<X> r, const X& v);    // find v in r

vector<X> vec;
// ...
auto p = find({vec.begin(), vec.end()}, X{});  // find X{} in vec
```

Ranges are extremely common in C++ code. Typically they are implicit, and their
correct use is very hard to ensure. In particular, given a pair of arguments
`(p, n)` designating an array `[p:p+n)`, it is in general impossible to know
whether there really are `n` elements to access following `*p`. `span<T>` and
`span_p<T>` are simple helper classes designating a `[p:q)` range and a range
starting with `p` and ending with the first element for which a predicate is
true, respectively.

A span represents a range of elements, but how are the elements of that range
manipulated? Every ordinary access is available, and the safest of them is the
one that needs no index at all:

```cpp
void f(span<int> s)
{
    // range traversal (guaranteed correct)
    for (int x : s) cout << x << '\n';

    // C-style traversal (potentially checked)
    for (gsl::index i = 0; i < s.size(); ++i) cout << s[i] << '\n';

    // random access (potentially checked)
    s[7] = 9;

    // extract pointers (potentially checked)
    std::sort(&s[0], &s[s.size() / 2]);
}
```

A `span<T>` object does not own its elements, and is so small that it can be
passed by value. Passing a span as an argument is exactly as efficient as
passing a pair of pointer arguments, or passing a pointer and an integer count.

## Decision

Designate a half-open sequence with a single range object, not with a pointer
and a separate bound.

- Take `std::span<T>` (or `std::span<const T>`) where a function operates on a
  contiguous sequence it does not own, and `std::string_view` where that
  sequence is character data.
- Prefer range-based `for` over index arithmetic inside the function; the
  traversal is then correct by construction rather than by inspection.
- Where the end of the sequence is defined by a predicate rather than a count,
  express that as a `span_p`-style range or an explicit predicate parameter,
  rather than leaving the reader to infer a sentinel convention.
- Pass a span by value. Do not take `const span<T>&`; the indirection costs more
  than the copy it avoids.
- Convert to a range type immediately inside any boundary that forces a
  `(pointer, count)` signature on you, and do not propagate the pair inward.

## Rationale

The decisive argument is that a span costs nothing over the form it replaces.
There is no efficiency trade to weigh: the same two words are passed either
way, and one of the two arrangements makes the relationship between them a
matter of type rather than a matter of convention.

That relationship is the whole point. `(p, n)` states two facts and no link
between them, so a caller can supply an `n` the callee has no means of
questioning; `span` states one fact that cannot be internally inconsistent.
Bounds then become checkable, which is the precondition for their ever being
checked.

## Consequences

### Positive

- The extent cannot disagree with the data, because there is only one object.
- Range-based traversal removes the off-by-one class of error from the body of
  the function, not merely from its signature.
- Overload sets shrink: one `span` overload replaces separate array, `vector`,
  and `(pointer, count)` forms.
- Calls read better, because the length is deduced at the call site rather than
  written by hand.

### Negative

- A span is a non-owning view with no lifetime guarantees of its own, so it
  dangles exactly as readily as the pointer it replaces while looking safer.
- Bounds are only *potentially* checked. Whether `s[7]` diagnoses anything
  depends on the standard library implementation and the build configuration,
  so the safety is partly aspirational.
- `span_p` is not a standard type. Predicate-terminated ranges have to be
  hand-rolled or expressed some other way, so half of this rule has no
  off-the-shelf implementation.
- Constructing a span from a pair of iterators, as in the `find` example above,
  reintroduces the two-values-that-must-agree problem at the call site.

### Neutral

- `std::span` is a C++20 standard library type, so from that standard onwards no
  support library is needed for the common case.
- How a range is stored inside a type is not this record's business; the rule is
  about what crosses a function signature.

## Enforcement

- (Complex) Warn where accesses to pointer parameters are bounded by another
  parameter of integral type, and suggest a span instead.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  rule this one gives the function-level form of.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — why the
  relationship between pointer and count belongs in the type.
- [0458](0458-f-25-use-a-zstring-or-a-not-null-zstring-to-designate-a-c-style-string.md)
  — the zero-terminated exception to passing sequences as ranges.
- [0461](0461-f-42-return-a-t-to-indicate-a-position-only.md) — the returning
  counterpart: a `T*` is a position, not a sequence.
