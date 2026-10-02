# 0143. Enum.5: Don't use ALL_CAPS for enumerators

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

The reason is to avoid clashes with macros.

```cpp
// webcolors.h (third party header)
#define RED   0xFF0000
#define GREEN 0x00FF00
#define BLUE  0x0000FF

// productinfo.h
// The following define product subtypes based on color
enum class Product_info { RED, PURPLE, BLUE };   // syntax error
```

The failure is worth following through, because it explains why the scoping that
solves every other collision does not solve this one. By the time the compiler
sees the enumeration, the preprocessor has already rewritten it to

```cpp
enum class Product_info { 0xFF0000, PURPLE, 0x0000FF };
```

which is not a declaration of anything. `enum class` protects the enumerators
from other C++ names, because C++ names live in scopes. A macro does not live in
a scope; it is a text substitution applied before scopes exist, and it will
replace the identifier wherever it appears, inside the enumeration's braces
included. The only defence available is not to spell an enumerator the way
macros are conventionally spelled.

## Decision

Do not name enumerators in ALL_CAPS.

- Name enumerators in the same style as other identifiers — `red`, `purple`,
  `blue` — and rely on the enumeration's own name for the context.
- Reserve ALL_CAPS for macros, so that seeing it in code is a reliable signal
  that the preprocessor is involved.
- When transcribing constants out of a C header or a macro block, rename them on
  the way in rather than preserving the shouting.
- Where a third-party header defines lower-case macros as well, treat its
  identifiers as reserved too and avoid them, or `#undef` them locally after
  inclusion.

## Rationale

This is a naming rule only in form; in substance it is a rule about the one
namespace C++ cannot protect. Every other name collision in the language has a
structural fix — a namespace, a class, a scoped enumeration. A macro collision
has none, so it has to be avoided by convention, and the convention only works
if one side of it gives ALL_CAPS up entirely.

The failure mode is also unusually bad. A clash between two macros silently
picks a value; a clash between a macro and an enumerator produces a syntax error
in a declaration that looks correct, pointing at a line whose text on screen is
not the text the compiler read.

## Consequences

### Positive

- The declaration cannot be destroyed by a macro from a header included above
  it.
- ALL_CAPS retains a meaning — this is a macro, the ordinary rules do not apply
  — instead of being one of two styles for constants.
- Enumerator names end up matching the surrounding code's style rather than
  standing out for a reason unrelated to what they are.

### Negative

- It is a convention, not a defence. Windows headers define lower-case `min` and
  `max` macros, and any codebase including them can still lose an enumerator
  spelled that way; this rule reduces the probability of a clash without
  removing the possibility.
- ALL_CAPS for constants is entrenched in C and in a great deal of existing C++,
  so the rule puts new code out of step with libraries it sits next to, and
  makes the two spellings of the same constant a translation everyone has to
  perform mentally.
- Renaming enumerators when adopting the rule is a source-breaking change for
  every caller, and for anything that stringifies the names into a file format
  or a log.
- Automated enforcement is crude: a checker can flag ALL_CAPS enumerators, but
  it cannot tell an unwise name from one that is merely unfashionable, and it
  cannot see the macros that would actually have collided.

### Neutral

- The rule constrains only the spelling of enumerators, not of the enumeration
  itself.
- It follows the same argument as
  [0139](0139-enum-1-prefer-enumerations-over-macros.md) from the other end:
  that record removes your macros, this one defends against everyone else's.

## Enforcement

Flag ALL_CAPS enumerators.

## Related

- [0139](0139-enum-1-prefer-enumerations-over-macros.md) — the macros this rule
  assumes are still out there.
- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — scoping fixes
  every collision except this one.
- [0009](0009-p-3-express-intent.md) — a name's style should say what the thing
  is, and ALL_CAPS already means "macro".
- [0144](0144-enum-6-avoid-unnamed-enumerations.md) — the other rule in this
  section about how an enumeration is written down rather than what it means.
