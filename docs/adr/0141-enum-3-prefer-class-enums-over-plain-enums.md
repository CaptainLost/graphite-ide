# 0141. Enum.3: Prefer class enums over "plain" enums

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

The point is to minimise surprises: traditional enums convert to `int` too
readily.

```cpp
void Print_color(int color);

enum Web_color { red = 0xFF0000, green = 0x00FF00, blue = 0x0000FF };
enum Product_info { red = 0, purple = 1, blue = 2 };

Web_color webby = Web_color::blue;

// Clearly at least one of these calls is buggy.
Print_color(webby);
Print_color(Product_info::blue);
```

Two failures are on display. The enumerators of a plain enumeration are injected
into the enclosing scope, so these two declarations cannot even coexist there —
`red` and `blue` are each declared twice. And the values convert to `int`
without a cast, so `Print_color` accepts both enumerations interchangeably even
though at most one of them can mean what it says.

Instead use an `enum class`:

```cpp
void Print_color(int color);

enum class Web_color { red = 0xFF0000, green = 0x00FF00, blue = 0x0000FF };
enum class Product_info { red = 0, purple = 1, blue = 2 };

Web_color webby = Web_color::blue;
Print_color(webby);  // Error: cannot convert Web_color to int.
Print_color(Product_info::red);  // Error: cannot convert Product_info to int.
```

The enumerators are now reachable only as `Web_color::red` and
`Product_info::red`, and neither type converts to the other or to the integer
the function actually wanted.

## Decision

Declare enumerations as `enum class`.

- Write `enum class` for every new enumeration, so the enumerators are scoped to
  it and do not convert implicitly to an integer type.
- Where an integer really is wanted, write the conversion out with
  `static_cast`, so that the place it happens is visible.
- Where the qualification genuinely hurts readability in a small scope, use a
  `using enum` declaration inside that scope rather than reaching for a plain
  enumeration; `using enum` is a C++20 facility.
- Leave plain enumerations where a C API or a third-party header defines them,
  and do not add new ones alongside.

## Rationale

The conversion is what makes a plain enumeration a weaker type than it appears.
It is declared as a distinct type, so it reads like one, but it silently
degrades to `int` at every call boundary — which means it participates in
overload resolution, arithmetic and comparison as an integer, and the compiler
never has occasion to object. A scoped enumeration is the type its declaration
already claimed to be.

Scoping is the cheaper half of the same win: an enumeration named after its
domain lets its enumerators be named plainly, so `Web_color::red` and
`Product_info::red` can both exist without anyone inventing a prefix.

## Consequences

### Positive

- Mixing two enumerations, or an enumeration and a raw count, stops compiling.
- Enumerator names no longer pollute the enclosing scope, so short natural names
  become usable again.
- Where a conversion to an integer is intended, it is written down, and so it
  can be found and reviewed.

### Negative

- Bit-flag enumerations become clumsy: `|` and `&` no longer work, so each set
  of flags needs operators defined for it, which is
  [0142](0142-enum-4-define-operations-on-enumerations-for-safe-and-simple-use.md)'s
  problem and real work.
- Every crossing into a C API, a serialisation routine, an array index or a
  variadic call needs an explicit `static_cast`, and casts scattered through
  boundary code are their own readability cost.
- Qualification makes call sites longer, and dense switch statements over a
  scoped enumeration repeat the type name on every label.
- The rule cannot be applied to enumerations that arrive from headers you do not
  control, so a codebase using such libraries has both kinds regardless.

### Neutral

- A scoped enumeration's default underlying type is `int`, which is what
  [0145](0145-enum-7-specify-the-underlying-type-of-an-enumeration-only-when-necessary.md)
  says to leave alone unless there is a reason.
- Scoping does not make the type exhaustive: a cast can still produce a value no
  enumerator names.

## Enforcement

(Simple) Warn on any non-class enum definition.

## Related

- [0142](0142-enum-4-define-operations-on-enumerations-for-safe-and-simple-use.md)
  — the operators a scoped enumeration now needs.
- [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md)
  — the enumerations this rule then asks you to scope.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  implicit conversion to `int` is precisely a hole in static type safety.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a parameter
  that accepts any enumeration is not strongly typed.
