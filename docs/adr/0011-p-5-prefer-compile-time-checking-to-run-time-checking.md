# 0011. P.5: Prefer compile-time checking to run-time checking

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Code clarity and performance both argue the same way here: an error caught at
compile time needs no error handler, no test, and no branch at run time.

The upstream example computes a property of a type by running code:

```cpp
// Int is an alias used for integers
int bits = 0;         // don't: avoidable code
for (Int i = 1; i; i <<= 1)
    ++bits;
if (bits < 32)
    cerr << "Int too small\n";
```

This fails to achieve what it is trying to achieve, because the overflow it
relies on is undefined, and should be replaced with a simple assertion the
compiler evaluates:

```cpp
// Int is an alias used for integers
static_assert(sizeof(Int) >= 4);    // do: compile-time check
```

Or better still, use the type system and replace `Int` with `int32_t`.

The second example is about interfaces:

```cpp
void read(int* p, int n);   // read max n integers into *p
int a[100];
read(a, 1000);              // bad, off the end
```

better:

```cpp
void read(span<int> r);     // read into the range of integers r
int a[100];
read(a);                    // better: let the compiler figure out the number
                            // of elements
```

An alternative formulation: don't postpone to run time what can be done well at
compile time.

## Decision

Prefer compile-time checking to run-time checking.

- Where a property of a type or a constant is being tested, use `static_assert`
  rather than code that computes it at run time.
- Where the property can be made true by construction instead of asserted,
  prefer that: a fixed-width type over an assertion about a type's width.
- Take parameters whose type carries the information the callee needs — a `span`
  rather than a pointer and a separately-passed count — so that the caller
  cannot supply an inconsistent pair.
- Use `constexpr` evaluation where a computation's inputs are known at compile
  time.

The qualifier in the alternative formulation is load-bearing: *what can be done
well* at compile time. A compile-time check that is convoluted, or that is
really a run-time condition in disguise, is not an improvement.

## Rationale

Every run-time check is code that must be written, tested, kept in sync with the
condition it checks, and executed. A compile-time check is none of those things
after it is written once, and it fails at the moment the mistake is made rather
than at the moment the mistake is reached.

The `span` half of the rule is the one with the most leverage in practice.
A `(pointer, count)` interface makes a wrong pairing expressible; a range type
makes it inexpressible, which is a stronger guarantee than any amount of
checking at either end.

## Consequences

### Positive

- Errors surface at build time, at the site of the mistake, with no test needed
  to provoke them.
- No run-time cost and no error-handling path for anything moved to compile
  time.
- Interfaces that carry their extents remove a whole class of caller mistake
  rather than diagnosing it.

### Negative

- Compile-time machinery is harder to read and to debug than the equivalent run
  time code, and template-heavy checks produce error messages that are worse
  than the bug they catch.
- Pushing checks into types can push complexity into interfaces, and a
  sufficiently clever type is its own maintenance burden.
- Longer build times, in the small.

### Neutral

- What to do with the checks that cannot move to compile time is
  [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  and [0013](0013-p-7-catch-run-time-errors-early.md), not this record.
- This record does not mandate `constexpr` on any particular function; that is a
  per-function judgement.

## Enforcement

- Look for pointer arguments.
- Look for run-time checks for range violations.

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  type-safety problem areas this rule leans on.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the fallback when a check cannot be static.
- [0009](0009-p-3-express-intent.md) — `f(span<T>)` over `f(T*, int)` appears
  there as an intent argument, and here as a checking argument.
