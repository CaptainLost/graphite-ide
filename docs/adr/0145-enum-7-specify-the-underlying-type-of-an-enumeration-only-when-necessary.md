# 0145. Enum.7: Specify the underlying type of an enumeration only when necessary

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

The default is the easiest to read and write. `int` is the default integer type,
and `int` is compatible with C enums.

```cpp
enum class Direction : char { n, s, e, w,
                              ne, nw, se, sw };  // underlying type saves space

enum class Web_color : int32_t { red   = 0xFF0000,
                                 green = 0x00FF00,
                                 blue  = 0x0000FF };  // underlying type is redundant
```

The two declarations look alike and are not. `Direction` has eight enumerators
and may be stored in quantity, so narrowing it to a byte buys something
measurable. `Web_color` names its underlying type as a 32-bit integer, which is
what it would have been anyway; the annotation adds a line's worth of reading
and changes nothing.

Specifying the underlying type is necessary in forward declarations of
enumerations, because the size of the type must be known at the point of use:

```cpp
enum Flags : char;
void f(Flags);

// ....

enum Flags : char { /* ... */ };
```

A scoped enumeration's underlying type is `int` unless one is given. An unscoped
enumeration without a fixed underlying type has one chosen by the
implementation, large enough to hold every enumerator — so for that kind, adding
the annotation is not only about size but about pinning something that would
otherwise vary. That is also why the forward declaration above must carry
`: char`: an opaque declaration of an unscoped enumeration may not omit the
underlying type, because there is otherwise nothing to determine it. A scoped
enumeration already has `int`, so `enum class Flags;` is well formed on its
own.

## Decision

Leave the underlying type off an enumeration unless there is a reason to give
one.

- Do not write `: int` on a scoped enumeration; that is the default.
- Give an underlying type when the storage genuinely matters — the enumeration
  is a member of a type held in bulk, or it is packed into an array or a
  structure whose layout is fixed.
- Give an underlying type when an unscoped enumeration is forward declared; that
  declaration is ill-formed without it, and the definition must then repeat it.
  A scoped enumeration needs no annotation to be forward declared.
- Give an underlying type when the enumeration's representation is part of a
  contract — a file format, a wire protocol, an interface to code compiled
  separately — so that the width is stated rather than inferred.
- When one is given, say in a comment what it is for, since the next reader's
  first question will be whether it is load-bearing.

## Rationale

An annotation that restates the default is worse than absent, because a reader
who sees it must assume it was deliberate and go looking for the reason. The
value of leaving it off is that the times it does appear are then informative:
`: char` on `Direction` is a statement that someone measured something.

The forward-declaration case is the one that keeps this from being a pure style
rule. There, the underlying type is not decoration but the only thing that makes
the declaration usable, and the requirement to repeat it in the definition is
what puts the two declarations at risk of disagreeing.

## Consequences

### Positive

- An underlying type in the source means something, because it is not written
  when it is not needed.
- Declarations stay short, and the enumerators are what the reader sees first.
- The default is the type that interoperates with C enums, so the common case
  needs no thought.

### Negative

- "Only when necessary" is a judgement, and the space argument in particular has
  no threshold: whether narrowing an enumeration is worth an annotation depends
  on how it is stored, which is not visible from the declaration.
- Narrowing to `char` inherits `char`'s implementation-defined signedness, so an
  enumeration written that way has a range that differs between compilers —
  a cost the `Direction` example does not mention.
- A header that wants to forward declare an unscoped enumeration forces the
  annotation on every definition of it, so the rule is overridden by a decision
  made elsewhere, and the two spellings must be kept in step by hand.
- Relying on the default leaves an unscoped enumeration's underlying type up to
  the implementation, which is fine until the value is written to disk or
  crosses an ABI, at which point the omission is the bug.

### Neutral

- The rule concerns the enumeration's type, not its values;
  [0146](0146-enum-8-specify-enumerator-values-only-when-necessary.md) is the
  parallel rule for those.
- Upstream gives no enforcement for this rule, and none is invented here.

## Related

- [0146](0146-enum-8-specify-enumerator-values-only-when-necessary.md) — the
  same "write it only when it earns its place" argument, applied to values.
- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — scoped
  enumerations are the ones whose default underlying type is `int`.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — narrowing the underlying type
  is the one reason here that is about cost rather than clarity.
- [0009](0009-p-3-express-intent.md) — an annotation that restates the default
  states no intent.
