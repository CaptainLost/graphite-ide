# 0140. Enum.2: Use enumerations to represent sets of related named constants

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

An enumeration shows the enumerators to be related and can be a named type.

```cpp
enum class Web_color { red = 0xFF0000, green = 0x00FF00, blue = 0x0000FF };
```

Two things are gained at once. The reader learns that these three constants
belong together and that nothing else does; the compiler learns a type it can
put in signatures, overload on, and check at every call.

Switching on an enumeration is common, and the compiler can warn against
unusual patterns of case labels. For example:

```cpp
enum class Product_info { red = 0, purple = 1, blue = 2 };

void print(Product_info inf)
{
    switch (inf) {
    case Product_info::red: cout << "red"; break;
    case Product_info::purple: cout << "purple"; break;
    }
}
```

Such off-by-one switch statements are often the result of an added enumerator
and insufficient testing. The value of the enumeration here is not that it
prevented the mistake — it did not — but that it left enough information in the
program for a tool to find it. A set of loose integer constants gives the
compiler nothing to compare the case labels against.

## Decision

Use an enumeration whenever a group of named integer constants forms a set.

- Declare the enumeration when the constants are alternatives from one domain:
  colours, states, kinds, modes. The test is whether a variable would ever
  sensibly hold one of them without saying which.
- Use the enumeration as the parameter and return type, rather than passing the
  underlying integer and documenting the meaning in a comment.
- Where the constants are not alternatives from one domain, do not group them
  into an enumeration merely to name them.
- Switch on the enumeration and cover every enumerator, or write a `default`
  that says what happens to the rest — but not both, since a `default` is what
  silences the exhaustiveness warning this rule buys.

## Rationale

The switch example is the sharpest argument, and it is an argument about tools
rather than about readability. Grouping the constants into a type is what makes
"you handled two of the three" a question the compiler is able to ask. Nothing
about a set of `constexpr int`s permits that question, so the mistake is found
by a user instead.

The naming half compounds it: once the set has a type, a function that takes a
`Product_info` cannot be handed a `Web_color`, an index, or a count, and the
domain of the parameter stops being something the caller has to read prose to
learn.

## Consequences

### Positive

- The compiler can warn about a switch that covers most but not all enumerators,
  which catches the enumerator added last.
- The set gets a name usable in signatures, so the domain of a parameter is
  stated in the type rather than in a comment.
- Adding an enumerator is a change with a blast radius the compiler can compute,
  instead of one found by testing.

### Negative

- A `default` label restores the silence: it is often necessary for values
  arriving from outside the program, and it turns off exactly the warning this
  rule was for.
- Exhaustiveness warnings are a quality-of-implementation matter and are off by
  default in some compilers, so the benefit depends on build configuration
  rather than on the language.
- Enumerating a set fixes it at compile time. A set that must be extended by
  configuration, plugins or data will outgrow the enumeration, and the
  intermediate state — an enumeration plus an escape value — is worse than
  either.
- An enumeration variable can hold a value no enumerator names, since a cast or
  a read from disk can produce one; the type is a statement of intent and not a
  guaranteed invariant.

### Neutral

- The rule does not say the enumeration must be scoped, though
  [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) does.
- Whether the enumerators need explicit values is a separate question, settled
  by [0146](0146-enum-8-specify-enumerator-values-only-when-necessary.md).

## Enforcement

- Flag switch statements where the cases cover most but not all enumerators of
  an enumeration.
- Flag switch statements where the cases cover a few enumerators of an
  enumeration, but there is no `default`.

## Related

- [0139](0139-enum-1-prefer-enumerations-over-macros.md) — the same grouping,
  argued against the preprocessor.
- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — why the
  enumeration should be scoped.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — an
  enumeration parameter instead of an `int` is this rule at an interface.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  exhaustiveness warning is checking moved to compile time.
