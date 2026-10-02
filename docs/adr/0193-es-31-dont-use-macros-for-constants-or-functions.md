# 0193. ES.31: Don't use macros for constants or "functions"

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Macros are a major source of bugs. They do not obey the usual scope rules, they
do not obey the usual type rules, and they do not obey the usual rules for
argument passing. They ensure that the human reader sees something different
from what the compiler sees, and they complicate tool building.

```cpp
#define PI 3.14
#define SQUARE(a, b) (a * b)
```

Even leaving aside the well-known bug left in `SQUARE` — the parameters are not
parenthesised, so `SQUARE(1 + 1, 2)` is `1 + 1 * 2`, and the name says "square"
while the definition takes two arguments — there are much better behaved
alternatives:

```cpp
constexpr double pi = 3.14;
template<typename T> T square(T a, T b) { return a * b; }
```

The replacements differ in every respect that matters. `pi` has a type, so it
participates in overload resolution and conversion rules rather than being
substituted as a token sequence. It has a scope, so it can live in a namespace
and be shadowed deliberately rather than colliding globally. `square` evaluates
each argument exactly once, so an argument with a side effect behaves as
written. Both can be stepped through in a debugger and both appear in the
symbol table.

## Decision

Express constants and computations in the language, not in the preprocessor.

- Define a constant as `constexpr` (or `const`) at namespace, class, or block
  scope, in the namespace where it belongs.
- Define a computation as a function, an `inline` function, a `constexpr`
  function, or a template — never as a function-like macro.
- Use an `enum class` for a related set of named integral values rather than a
  block of `#define`s.
- When replacing an existing macro, rename it out of capitals at the same time,
  per [0179](0179-es-9-avoid-all-caps-names.md).
- Do not reach for a macro because a function would not be a constant
  expression; `constexpr` covers that case, and `consteval` covers the case
  where compile-time evaluation must be guaranteed.

## Rationale

A `constexpr` object and a `constexpr` function do everything a macro constant
and a macro function were reaching for — evaluation at compile time, no run-time
cost — while being typed, scoped, and visible to every tool. The macro form has
no remaining advantage, only remaining habits. What the macro adds is a distinct
class of bug that has no analogue in the function form: arguments substituted as
text, so precedence surprises and multiple evaluation of side effects both
become possible, and neither is visible at the call site.

## Consequences

### Positive

- Constants and functions get types, so misuse is a compile error rather than a
  strange expansion.
- Arguments are evaluated once, in the order the language specifies, so side
  effects behave.
- Namespacing works, so two components can each have a `pi` without a collision.

### Negative

- A `constexpr` variable at namespace scope is an object with an address, which
  a `#define` was not. Declared in a header it is `const` and so has internal
  linkage, giving every translation unit its own copy at its own address;
  `inline constexpr` is needed for one shared object. That is a subtlety the
  macro form did not have.
- A template function is not a drop-in replacement where the macro was
  deliberately type-agnostic across types that share no common concept.
- Conditional compilation of a constant's *value* per platform still goes
  through the preprocessor, so `#if` blocks around `constexpr` definitions
  remain.
- An assertion facility that reports the text of the condition it checked
  cannot be a function, because only the preprocessor can turn an expression
  into a string; some function-like macros therefore survive this rule. The
  call site itself is not the obstacle — `std::source_location` supplies that
  to an ordinary function.

### Neutral

- `constexpr` and templates were not available when the macro habit formed;
  this rule is largely about retiring it rather than about a live trade-off.
- Macros for configuration control are governed by
  [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md), not here.

## Enforcement

Scream when you see a macro that is not being used for source control, for
example `#ifdef`.

## Related

- [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) — the same
  reasoning for the preprocessor's other main use.
- [0188](0188-es-25-declare-an-object-const-or-constexpr-unless-you-want-to-modify-its-value-later-on.md)
  — `constexpr` is the replacement for a macro constant.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — a macro
  is untyped by construction.
- [0179](0179-es-9-avoid-all-caps-names.md) — do not keep the capitals when the
  macro becomes a constant.
