# 0011. P.5: Prefer compile-time checking to run-time checking

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

This codebase checks almost everything at run time and returns
`Result<T>` when a check fails. P.5 asks which of those checks did not need to
exist, because the type system could have carried the guarantee instead.

## Decision

Adopt P.5 as written. Transcribed in full:

> **Reason.** Code clarity and performance. You don't need to write error
> handlers for errors caught at compile time.

**Example.**

```cpp
// Int is an alias used for integers
int bits = 0;         // don't: avoidable code
for (Int i = 1; i; i <<= 1)
    ++bits;
if (bits < 32)
    cerr << "Int too small\n";
```

This example fails to achieve what it is trying to achieve (because overflow is
undefined) and should be replaced with a simple `static_assert`:

```cpp
// Int is an alias used for integers
static_assert(sizeof(Int) >= 4);    // do: compile-time check
```

Or better still just use the type system and replace `Int` with `int32_t`.

**Example.**

```cpp
void read(int* p, int n);   // read max n integers into *p

int a[100];
read(a, 1000);    // bad, off the end
```

better

```cpp
void read(span<int> r); // read into the range of integers r

int a[100];
read(a);        // better: let the compiler figure out the number of elements
```

Alternative formulation: Don't postpone to run time what can be done well at
compile time.

**Enforcement.**

- Look for pointer arguments.
- Look for run-time checks for range violations.

## Rationale

Adopted as written. The rule's second example is the one that applies here: a
`span` parameter carries its own length, so a whole class of run-time check
disappears rather than being handled.

`static_assert` is cheap and underused in this codebase; the rule is a standing
invitation to convert an invariant that is checked at run time into one the
build refuses to violate.

## Consequences

### Positive

- Where it applies, it removes an error path entirely — which matters more here
  than usual, because every error path in this codebase costs a `Result<T>` and
  a handler at each call site (see
  [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)).

### Negative

- Most of what this program does is inherently run-time: files exist or do not,
  a `.slnx` parses or does not. P.5 has little purchase on the majority of the
  checks in `Fs` and `Sln`, so it can read as more applicable than it is.

### Neutral

- Compile-time checking here means `static_assert` and stronger types; the
  project uses no `constexpr` evaluation of any substance and this rule does not
  ask it to.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `std::span` is in the precompiled header but used nowhere; length-carrying parameters are not the norm | `Core/src/corepch.h:13`; parameter lists throughout `Fs`, `Sln`, `Text` | The single most actionable item from this rule |

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  previous rule.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the next rule, which takes over where this one stops.
