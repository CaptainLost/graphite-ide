# 0139. Enum.1: Prefer enumerations over macros

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

Macros do not obey scope and type rules. Also, macro names are removed during
preprocessing and so usually do not appear in tools like debuggers.

First some bad old code:

```cpp
// webcolors.h (third party header)
#define RED   0xFF0000
#define GREEN 0x00FF00
#define BLUE  0x0000FF

// productinfo.h
// The following define product subtypes based on color
#define RED    0
#define PURPLE 1
#define BLUE   2

int webby = BLUE;   // webby == 2; probably not what was desired
```

Two headers each claim the name `BLUE`, and whichever was included last wins.
Redefining an object-like macro with a different replacement list is ill-formed,
so a compiler will normally warn about it — but a warning in a translation unit
that includes hundreds of headers is easily lost, and it is the only notice
there will be. By the time the compiler proper sees `int webby = BLUE;` there
are no names left to collide, only the integer they expanded to, so no scope or
type rule can be brought to bear on the mistake.

Instead use an enum:

```cpp
enum class Web_color { red = 0xFF0000, green = 0x00FF00, blue = 0x0000FF };
enum class Product_info { red = 0, purple = 1, blue = 2 };

int webby = blue;   // error: be specific
Web_color webby = Web_color::blue;
```

An `enum class` was used here to avoid the name clash: each `blue` is scoped to
its own enumeration, and neither is reachable unqualified.

## Decision

Define named integer constants as enumerators rather than as macros.

- Give a set of related integer constants a named enumeration, and prefer
  `enum class` so the enumerator names are scoped to it.
- Where the constants are not related to one another, use `constexpr` variables
  rather than either a macro or an enumeration.
- Reserve the preprocessor for what only the preprocessor can do: include
  guards, and conditional compilation on things the compiler cannot see.
- When a third-party header forces macros on the code, do not spread them: name
  the enumeration and its enumerators so that they cannot collide with what that
  header defines.

## Rationale

The debugger argument is the one a macro cannot answer at any price. A macro
constant is gone before the compiler starts, so it cannot appear in the type of
anything, in an overload set, or in the debug information; the value is all that
survives, and a value is exactly what a reader cannot interpret. An enumerator
is a name the whole toolchain can see.

Scope is the second half. Two macro constants that share a spelling are a silent
redefinition; two enumerators that share a spelling in different scoped
enumerations are simply two different things, and the language keeps them apart
without anyone having to agree on a prefix convention.

## Consequences

### Positive

- The constants acquire a type and a scope, so a collision becomes a compile
  error instead of a quiet substitution.
- The names survive into debug information, so a debugger shows
  `Web_color::blue` rather than `16711680`.
- Related constants are visibly grouped, and a function can take the enumeration
  itself as a parameter type instead of an `int`.

### Negative

- Macros from headers you do not own remain, and a macro can still wreck an
  enumeration that spells an enumerator the same way. That hole is what
  [0143](0143-enum-5-dont-use-all-caps-for-enumerators.md) exists to narrow; it
  cannot be closed from this side.
- Enumerators are not usable in `#if`, so a constant that conditional
  compilation consumes has to stay a macro, and a constant needed in both places
  ends up written twice.
- A macro expands to arbitrary tokens; an enumerator must be an integral
  constant. Not every macro constant has an enumeration replacement.
- Replacing macros with `enum class` costs the implicit conversion to `int`, so
  arithmetic and bitwise uses need casts or operators defined for them.

### Neutral

- The rule says nothing about which values the constants take;
  [0146](0146-enum-8-specify-enumerator-values-only-when-necessary.md) covers
  when they need to be written out at all.
- A plain `enum` satisfies the letter of this rule;
  [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) narrows it further.

## Enforcement

Flag macros that define integer values.

## Related

- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — the scoping this
  rule's fix depends on.
- [0143](0143-enum-5-dont-use-all-caps-for-enumerators.md) — the residual macro
  hazard, addressed by naming.
- [0144](0144-enum-6-avoid-unnamed-enumerations.md) — where `constexpr`, not an
  enumeration, is the right replacement for a macro.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — a macro states a value;
  an enumeration states an idea.
