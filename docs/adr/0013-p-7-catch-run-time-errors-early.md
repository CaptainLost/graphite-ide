# 0013. P.7: Catch run-time errors early

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

Directly relevant to how this codebase validates paths and solution files: the
rule's "don't validate twice" and "don't pass structured data as strings"
examples both describe patterns that appear when a `std::filesystem::path`
becomes a `std::string` and back.

## Decision

Adopt P.7 as written. Transcribed in full:

> **Reason.** Avoid "mysterious" crashes. Avoid errors leading to (possibly
> unrecognized) wrong results.

**Example.**

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

Here we made a small error in `use1` that will lead to corrupted data or a
crash. The (pointer, count)-style interface leaves `increment1()` with no
realistic way of defending itself against out-of-range errors. If we could check
subscripts for out of range access, then the error would not be discovered until
`p[10]` was accessed. We could check earlier and improve the code:

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

Now, `m <= n` can be checked at the point of call (early) rather than later. If
all we had was a typo so that we meant to use `n` as the bound, the code could
be further simplified (eliminating the possibility of an error):

```cpp
void use3(int m)
{
    const int n = 10;
    int a[n] = {};
    // ...
    increment2(a);   // the number of elements of a need not be repeated
    // ...
}
```

**Example, bad.** Don't repeatedly check the same value. Don't pass structured
data as strings:

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

The date is validated twice (by the `Date` constructor) and passed as a
character string (unstructured data).

**Example.** Excess checking can be costly. There are cases where checking early
is inefficient because you might never need the value, or might only need part
of the value that is more easily checked than the whole. Similarly, don't add
validity checks that change the asymptotic behavior of your interface (e.g.,
don't add a O(n) check to an interface with an average complexity of O(1)).

```cpp
class Jet {    // Physics says: e * e < x * x + y * y + z * z
    float x;
    float y;
    float z;
    float e;
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

    ???
};
```

The physical law for a jet (`e * e < x * x + y * y + z * z`) is not an invariant
because of the possibility for measurement errors.

**Enforcement.**

- Look at pointers and arrays: Do range-checking early and not repeatedly
- Look at conversions: Eliminate or mark narrowing conversions
- Look for unchecked values coming from input
- Look for structured data (objects of classes with invariants) being converted
  into strings

## Rationale

Adopted as written, and it is the philosophy rule with the most direct bearing
on the code as it stands. Two of its four enforcement points describe live
patterns here: values arriving unchecked from input — a hand-edited `.slnx`, a
settings JSON, a path typed into a dialog — and structured data flowing as
strings, which is what happens every time a `path` is stringified to cross an
interface.

The third example is also worth keeping in view, because it argues the other
way: checking early is not free, and a validity check must not change the
complexity of the interface it guards. That is the reason `FileTree` expands
lazily rather than validating a whole subtree up front.

## Consequences

### Positive

- Gives a name to the rule `JsonFile` already follows: a hand-edited file must
  not be able to crash the application, so it is validated at the boundary once
  and trusted afterwards.
- The "don't check twice" half is a real economy in `Sln`, where the same path
  passes through several readers.

### Negative

- "Early" and "twice" are judgement calls. The rule cannot say where the
  boundary is, so it does not settle an argument about whether a given check
  belongs in the reader or in the caller.
- Following the fourth point strictly would mean fewer `std::string` parameters
  and more small types, which costs conversions at every Win32 and ImGui call —
  both of which want a raw string.

### Neutral

- The `???` markers in the third example are transcribed as they appear
  upstream.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| Paths converted to `std::string` and back across interfaces, losing the type that carries the invariant | `Core/src/Core/Sln/*`, `Graphite/src/Workspace.cpp` | The "structured data as strings" pattern the rule names |
| Narrowing conversions neither eliminated nor marked | `Fs`, `Text` | Same item as in [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) |

## Related

- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the previous rule.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the next rule.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  what happens to an error once it is caught.
