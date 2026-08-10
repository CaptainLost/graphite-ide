# 0393. SL.con.3: Avoid bounds errors

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Reading or writing beyond an allocated range of elements typically leads to bad
errors, wrong results, crashes, and security violations.

The standard-library functions that apply to ranges of elements all have, or
could have, bounds-safe overloads that take a `span`. Standard types such as
`vector` can be modified to perform bounds checks under the bounds profile, in a
compatible way such as by adding contracts, or used with `at()`.

Ideally the in-bounds guarantee should be enforced statically. A range-`for`
cannot loop beyond the range of the container it is applied to, and a loop over
`v.begin()`, `v.end()` is easily determined to be bounds safe. Such loops are as
fast as any unchecked equivalent. Often a simple pre-check removes the need to
check individual indices: for `v.begin()`, `v.begin() + i` the `i` can easily be
checked once against `v.size()`, and such loops can be much faster than
individually checked element accesses.

```cpp
void f()
{
    array<int, 10> a, b;
    memset(a.data(), 0, 10);         // BAD, and contains a length error (length = 10 * sizeof(int))
    memcmp(a.data(), b.data(), 10);  // BAD, and contains a length error (length = 10 * sizeof(int))
}
```

`std::array<>::fill()`, `std::fill()`, or even an empty initialiser are better
candidates than `memset()`:

```cpp
void f()
{
    array<int, 10> a, b, c{};       // c is initialized to zero
    a.fill(0);
    fill(b.begin(), b.end(), 0);    // std::fill()
    fill(b, 0);                     // std::ranges::fill()
    if ( a == b ) {
      // ...
    }
}
```

If the code uses an unmodified standard library, there are still workarounds
that allow `std::array` and `std::vector` to be used in a bounds-safe manner.
Code can call the `.at()` member function, which throws `std::out_of_range`, or
the `at()` free function, which fails fast or performs a customised action on a
bounds violation:

```cpp
void f(std::vector<int>& v, std::array<int, 12> a, int i)
{
    v[0] = a[0];        // BAD
    v.at(0) = a[0];     // OK (alternative 1)
    at(v, 0) = a[0];    // OK (alternative 2)
    v.at(0) = a[i];     // BAD
    v.at(0) = a.at(i);  // OK (alternative 1)
    v.at(0) = at(a, i); // OK (alternative 2)
}
```

## Decision

Make every element access provably in bounds, statically where possible and by
a check where not.

- Prefer a range-`for` or an algorithm over an index loop; a loop that cannot
  name an out-of-range index cannot produce one.
- Where an index is unavoidable, check it once against the size before the loop
  rather than on every access.
- Where an index arrives from outside and cannot be pre-checked, use a checked
  access rather than `operator[]`.
- Never pass an element count where a standard C function expects a byte count;
  `memset(a.data(), 0, 10)` on ten `int`s clears two and a half of them.
- Prefer `fill`, `ranges::fill`, value initialisation and `operator==` to
  `memset` and `memcmp` on standard containers.

## Rationale

The sharpest point in the source is that bounds safety is usually free. A
range-`for` and a pre-checked loop compile to the same code as the unchecked
version, so the common objection — that checking costs performance — applies
only to the case where an index genuinely arrives unvalidated, which is exactly
the case that needs the check.

The `memset(a.data(), 0, 10)` example is worth keeping because it is wrong in a
way no reviewer spots by reading: the call is well formed, the constant matches
the array's element count, and the bug is the unit.

## Consequences

### Positive

- The common loop forms become bounds-safe with no run-time cost and no extra
  code.
- Checking moves from per-access to per-range, which is both cheaper and easier
  to reason about.
- Element-count versus byte-count confusion disappears along with the raw
  `mem*` calls.

### Negative

- `.at()` reports a violation by throwing `std::out_of_range`, so adopting it
  commits the caller to an exception-handling strategy. Code that cannot or does
  not throw has to use the fail-fast alternative instead, and the two behave
  entirely differently on the same defect.
- The free `at()` the guidelines use is a GSL facility, not a standard one, so
  the recommended second alternative is not available without that dependency.
- Bounds checking on `operator[]` is a quality-of-implementation and build-flag
  matter, so whether a violation is caught differs between debug and release
  builds — the worst possible place for a behavioural difference.
- Rewriting an index loop as an algorithm call is not always clearer, and the
  rule can push readable code into unreadable iterator gymnastics.

### Neutral

- The rule is part of the guidelines' bounds profile, and reads best as one
  clause of that profile rather than in isolation.
- Upstream notes that bounds-safe overloads of C standard library functions such
  as `memcmp` are still only under consideration for the GSL, so the safe
  replacement for some of these calls does not yet exist in standard form.

## Enforcement

- Issue a diagnostic for any call to a standard-library function that is not
  bounds-checked. Upstream leaves the list of such functions unwritten.

## Related

- [0394](0394-sl-con-4-dont-use-memset-or-memcpy-for-arguments-that-are-not-trivially-copyable.md)
  — the other half of the objection to the `mem*` family.
- [0208](0208-es-55-avoid-the-need-for-range-checking.md) — the same preference
  for loops that cannot go out of range.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the philosophy rule behind checked access.
- [0390](0390-sl-4-use-the-standard-library-in-a-type-safe-manner.md) — the
  umbrella rule this is one of the concrete forms of.
