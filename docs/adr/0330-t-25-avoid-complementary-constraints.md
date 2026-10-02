# 0330. T.25: Avoid complementary constraints

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Clarity and maintainability. Functions with complementary requirements expressed
using negation are brittle. Initially, people will try to define functions with
complementary requirements:

```cpp
template<typename T>
    requires !C<T>    // bad
void f();

template<typename T>
    requires C<T>
void f();
```

This is better:

```cpp
template<typename T>   // general template
    void f();

template<typename T>   // specialization by concept
    requires C<T>
void f();
```

The compiler will choose the unconstrained template only when `C<T>` is
unsatisfied. If an unconstrained version of `f()` is not wanted, or cannot be
defined, delete it:

```cpp
template<typename T>
void f() = delete;
```

The compiler will then select the overload or emit an appropriate error.

Complementary constraints are unfortunately common in `enable_if` code:

```cpp
template<typename T>
enable_if<!C<T>, void>   // bad
f();

template<typename T>
enable_if<C<T>, void>
f();
```

Complementary requirements on one requirement are sometimes, wrongly, considered
manageable. For two or more requirements the number of definitions needed goes up
exponentially — 2, 4, 8, 16 — as in `C1<T> && C2<T>`, `!C1<T> && C2<T>`,
`C1<T> && !C2<T>`, `!C1<T> && !C2<T>`. The opportunities for errors multiply with
them.

## Decision

Express a specialisation by adding a constraint, never by negating one.

- Write the general case unconstrained and the special case constrained, and let
  partial ordering of constraints choose between them.
- Where there is no sensible general case, define it as `= delete` so that an
  unmatched type produces a clear error rather than a silent fallback.
- Do not write a `requires !C<T>` overload paired with a `requires C<T>` overload.
- Do not reproduce the pattern with `enable_if`, which is where it most often
  appears; see
  [0339](0339-t-48-if-your-compiler-does-not-support-concepts-fake-them-with-enable-if.md).

## Rationale

The unconstrained overload is already the negation, computed by the compiler and
guaranteed to stay consistent, whereas a hand-written `!C<T>` is a second
statement that must be kept in step with the first by hand. The decisive argument
is the counting one: with two independent predicates the negated style needs four
definitions and with three it needs eight, while the constrained-specialisation
style needs one more overload per case actually handled.

## Consequences

### Positive

- Adding a new constrained case does not require editing the existing ones.
- The overload set cannot develop a gap or an overlap, since the fallback is
  whatever the constraints did not claim.
- `= delete` gives a precise diagnostic where the older style gave a fallback
  nobody intended.
- Constraint subsumption does the ordering, so no tag dispatch is needed.

### Negative

- It depends on partial ordering by constraints being well understood; when the
  ordering is not what the author expected, the failure is an ambiguity error
  whose explanation is hard to read.
- Once there is more than one constrained overload, subsumption compares atomic
  constraints syntactically: two constraints that mean the same thing but are
  written out separately are unordered, and the call is ambiguous. The remedy is
  to define one concept in terms of the other, which this rule does not say.
- An unconstrained general template is exactly what
  [0338](0338-t-47-avoid-highly-visible-unconstrained-templates-with-common-names.md)
  warns against, so the recommended shape here introduces a hazard elsewhere and
  makes `= delete` the safer default more often than the text suggests.
- Existing `enable_if`-based code cannot be converted piecemeal; the whole
  overload set has to move at once.

### Neutral

- The rule concerns how overload sets are separated, not what the constraints
  themselves say.
- The `enable_if` examples are pre-C++20; where real constraints are available
  the `enable_if` form should not be reached for at all.

## Enforcement

- Flag pairs of functions constrained with `C<T>` and `!C<T>`.

## Related

- [0339](0339-t-48-if-your-compiler-does-not-support-concepts-fake-them-with-enable-if.md)
  — where complementary constraints usually come from.
- [0338](0338-t-47-avoid-highly-visible-unconstrained-templates-with-common-names.md)
  — the cost of the unconstrained general template this rule recommends.
- [0083](0083-c-81-use-delete-when-you-want-to-disable-default-behavior-without-wanting-an-alternative.md)
  — the `= delete` used here for the same purpose.
- [0328](0328-t-23-differentiate-a-refined-concept-from-its-more-general-case-by-adding-new-use-patterns.md)
  — the supported way to make two overloads distinguishable.
