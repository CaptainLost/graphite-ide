# 0013. P.7: Catch run-time errors early

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Catching an error late means a mysterious crash, or worse, a wrong result nobody
recognises as wrong. The upstream example is a range error that the interface
cannot defend against:

```cpp
void increment1(int* p, int n)    // bad: error-prone
{
    for (int i = 0; i < n; ++i) ++p[i];
}
void use1(int m)
{
    const int n = 10;
    int a[n] = {};
    // ...
    increment1(a, m);   // maybe typo, maybe m <= n is supposed
                        // but assume that m == 20
    // ...
}
```

A small error in `use1` leads to corrupted data or a crash. The
`(pointer, count)` interface leaves `increment1()` with no realistic way of
defending itself against out-of-range errors; even with subscript checking, the
error would not be discovered until `p[10]` was accessed. Checking earlier
improves it:

```cpp
void increment2(span<int> p)
{
    for (int& x : p) ++x;
}
void use2(int m)
{
    const int n = 10;
    int a[n] = {};
    // ...
    increment2({a, m});    // maybe typo, maybe m <= n is supposed
    // ...
}
```

Now `m <= n` can be checked at the point of call rather than later. If the only
problem was a typo, and `n` was meant, the possibility of the error disappears
entirely:

```cpp
increment2(a);   // the number of elements of a need not be repeated
```

A second failure mode is checking the same thing repeatedly, usually because
structured data was flattened into a string and has to be re-validated:

```cpp
Date read_date(istream& is);           // read date from istream
Date extract_date(const string& s);    // extract date from string
void user1(const string& date)         // manipulate date
{
    auto d = extract_date(date);
    // ...
}
void user2()
{
    Date d = read_date(cin);
    // ...
    user1(d.to_string());
    // ...
}
```

The date is validated twice, by the `Date` constructor, and passed between the
two as unstructured data.

Excess checking can also be costly. There are cases where checking early is
inefficient because the value might never be needed, or because only part of it
is needed and that part is more easily checked than the whole. Similarly,
validity checks should not change the asymptotic behaviour of an interface — do
not add an O(n) check to an interface with an average complexity of O(1). And
some conditions are not invariants at all:

```cpp
class Jet {    // Physics says: e * e < x * x + y * y + z * z
    float x, y, z, e;
public:
    Jet(float x, float y, float z, float e)
        :x(x), y(y), z(z), e(e)
    {
        // Should I check here that the values are physically meaningful?
    }
    float m() const
    {
        // Should I handle the degenerate case here?
        return sqrt(x * x + y * y + z * z - e * e);
    }
};
```

The physical law for a jet is not an invariant, because of the possibility of
measurement error.

## Decision

Check a run-time error at the earliest point where it can be detected — and
check it once.

- Check a range at the point where the range is constructed, not at the point
  where an element is finally touched.
- Validate input where it enters the program, then keep it in a type that
  guarantees the validated property, so that later code does not re-check.
- Do not pass structured data as strings between parts of the program; pass the
  type whose invariant was already established.
- Do not add a check that changes the asymptotic complexity of the operation it
  guards, and do not check what might never be used.
- Do not turn a condition that is merely usually true — a physical law subject to
  measurement error, say — into a class invariant.

## Rationale

Early checking is worth having because the distance between the mistake and its
symptom is what makes debugging expensive; the check's value is in *where* it
fires, not that it fires.

The two limits in the decision are what keep the rule honest. Repeated checking
costs time and, worse, spreads the definition of "valid" across the codebase, so
that the copies drift. And a check on the wrong condition — one that does not
actually always hold — converts a merely inaccurate result into a hard failure,
which is a downgrade.

## Consequences

### Positive

- Failures point at their cause instead of at some later innocent code.
- A validated value carried in its own type is validated once, in one place, by
  one piece of code.
- Removing string round-trips removes both the duplicate validation and the
  chance that the two parsers disagree.

### Negative

- Early checks cost time on values that may never be used, and the rule requires
  judgement about when that cost is worth paying — judgement this record cannot
  supply per call site.
- Deciding what belongs in a class invariant is genuinely hard; the `Jet` case
  has no clean answer upstream either.
- Introducing validated types for data that arrives as text is a real amount of
  code.

### Neutral

- How a detected error is reported — exception, error code, result type — is not
  decided here; see
  [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md).
- This rule presupposes that checking is feasible at all, which is
  [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md).

## Enforcement

- Look at pointers and arrays: do range-checking early and not repeatedly.
- Look at conversions: eliminate or mark narrowing conversions.
- Look for unchecked values coming from input.
- Look for structured data — objects of classes with invariants — being
  converted into strings.

## Related

- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the interface property that makes early checking possible.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  checks that should not be run-time checks at all.
