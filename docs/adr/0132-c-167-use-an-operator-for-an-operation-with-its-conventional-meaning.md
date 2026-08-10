# 0132. C.167: Use an operator for an operation with its conventional meaning

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reasons are readability, convention, reusability and support for generic
code.

```cpp
void cout_my_class(const My_class& c) // confusing, not conventional, not generic
{
    std::cout << /* class members here */;
}

std::ostream& operator<<(std::ostream& os, const my_class& c) // OK
{
    return os << /* class members here */;
}
```

By itself, `cout_my_class` would be acceptable, but it is not usable or
composable with code that relies on the `<<` convention for output:

```cpp
My_class var { /* ... */ };
// ...
cout << "var = " << var << '\n';
```

There are strong and vigorous conventions for the meaning of most operators:
comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=` and `<=>`), arithmetic operations
(`+`, `-`, `*`, `/` and `%`), access operations (`.`, `->`, unary `*` and `[]`),
and assignment (`=`). Do not define those unconventionally, and do not invent
your own names for them.

## Decision

Where an operation has a conventional operator, give it that operator rather
than a name.

- Provide `operator<<` for output rather than a `print_x` or `dump_x` function.
- Provide `operator==` and `operator<=>` for comparison rather than an `equals`
  or `compare` member.
- Provide the arithmetic and access operators where the operation is genuinely
  arithmetic or genuinely access.
- Take the conventional signature as well as the conventional name — a stream
  inserter takes and returns `std::ostream&`, so that insertions chain.
- Do not invent an alternative name for an operation an operator already
  denotes, and do not give an operator a meaning that is not its conventional
  one.

## Rationale

The complaint against `cout_my_class` is not that it is ugly; it is that it does
not compose. `cout << "var = " << var << '\n'` works because every participant
in the chain agreed on one spelling, and a type that opted out cannot be placed
in the chain at all — not by its author and not by anyone else, ever. An
operator is therefore not decoration but an interface that pre-existing code has
already been written against.

## Consequences

### Positive

- The type works with code written before it existed, because that code was
  written against the convention.
- Generic algorithms and containers, which call `<`, `==` and `[]` by their
  operator spellings, accept the type without adaptation.
- Readers need learn no per-type vocabulary for operations they already
  understand.

### Negative

- The conventional signatures come with obligations: an inserter must return the
  stream, comparisons must be consistent with one another, and getting one of
  these subtly wrong is worse than not providing the operator.
- Operators cannot be named, cannot be found by searching for a word, and are
  harder to locate in a large codebase than `print_my_class` would be.
- They cannot carry extra arguments, so an output operation needing formatting
  options does not fit `<<` and needs a named function anyway.
- Operators cannot be virtual, so a type in a hierarchy needs a non-member
  operator forwarding to a virtual function.

### Neutral

- The rule tells you which spelling to use for an operation you have decided to
  provide; it does not require providing any.
- It is the mirror image of
  [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md):
  one forbids unconventional meanings for operators, this one forbids
  unconventional spellings for conventional meanings.

## Enforcement

- Tricky. Requires semantic insight.

## Related

- [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) —
  the converse rule; the two bound the same decision from opposite sides.
- [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md) — the
  conventional signature for comparisons and arithmetic is a non-member one.
- [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md)
  — where the operator must be defined for callers to find it.
- [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md) — `<<`
  is the largest overload set of equivalent operations in the language.
