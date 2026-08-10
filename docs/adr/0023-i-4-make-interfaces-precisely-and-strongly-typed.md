# 0023. I.4: Make interfaces precisely and strongly typed

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Types are the simplest and best documentation, improve legibility because their
meaning is well defined, and are checked at compile time. Precisely typed code
is also often optimised better.

The weakest possible parameter type is the obvious first example:

```cpp
void pass(void* data);    // weak and under qualified type void* is suspicious
```

Callers are unsure what types are allowed, and whether the data may be mutated,
since `const` is not specified. All pointer types implicitly convert to `void*`,
so it is easy for callers to provide this value. The callee must `static_cast`
the data to an unverified type to use it, which is error-prone and verbose. Only
use `const void*` for passing data in designs that are indescribable in C++;
consider a `variant` or a pointer to base instead. Often a template parameter
can eliminate the `void*`, turning it into a `T*` or `T&`, and for generic code
those `T`s can be general or concept-constrained.

Unnamed built-in parameters are the second example:

```cpp
draw_rect(100, 200, 100, 500); // what do the numbers specify?
draw_rect(p.x, p.y, 10, 20);   // what units are 10 and 20 in?
```

It is clear that the caller is describing a rectangle, but not what the parts
mean. An `int` can carry arbitrary forms of information, including values in
many units. Comments and parameter names help; being explicit is better:

```cpp
void draw_rectangle(Point top_left, Point bottom_right);
void draw_rectangle(Point top_left, Size height_width);
draw_rectangle(p, Point{10, 20});  // two corners
draw_rectangle(p, Size{10, 20});   // one corner and a (height, width) pair
```

Not all errors can be caught through the static type system: that the first
argument is supposed to be a top-left point is still left to convention.

Booleans at a call site are the third:

```cpp
set_settings(true, false, 42); // what do the numbers specify?
```

The parameter types and their values communicate neither which settings are
being specified nor what the values mean. This design is more explicit, safe and
legible:

```cpp
alarm_settings s{};
s.enabled = true;
s.displayMode = alarm_settings::mode::spinning_light;
s.frequency = alarm_settings::every_10_seconds;
set_settings(s);
```

For a set of boolean values, consider a flags enum:

```cpp
enable_lamp_options(lamp_option::on | lamp_option::animate_state_transitions);
```

And units are the fourth:

```cpp
void blink_led(int time_to_blink);      // bad -- the unit is ambiguous
void blink_led(milliseconds time_to_blink);  // good -- the unit is explicit
blink_led(1500ms);
```

The function can also accept any duration unit:

```cpp
template<class rep, class period>
void blink_led(duration<rep, period> time_to_blink)
{
    auto milliseconds_to_blink = duration_cast<milliseconds>(time_to_blink);
    // ...
}
```

## Decision

Make interfaces precisely and strongly typed.

- Do not take `void*`. Use a template parameter, a `variant`, or a pointer to a
  base class. `const void*` is reserved for designs genuinely indescribable in
  C++.
- Do not spell out a compound value as separate built-in parameters. Pass the
  type that means the thing: `Point`, `Size`, a settings struct.
- Do not take more than one `bool` parameter. Use an enum, a flags enum, or a
  named settings object.
- Carry units in the type. Prefer `std::chrono` duration types to raw integer
  counts of unspecified units.
- Specify `const` on parameters that are not modified, so mutability is stated
  rather than guessed.

## Rationale

Every item above converts a question a caller must answer from documentation
into one the compiler answers. That is worth more than the documentation because
it is checked, and it stays checked after the documentation stops being read.

The `bool` and unit cases are the ones that actually bite in practice: both
produce call sites that are wrong and compile, and both are trivially prevented
by a type that exists only to be unambiguous.

## Consequences

### Positive

- Call sites become legible without consulting the declaration.
- Argument mix-ups become compile errors instead of runtime behaviour.
- Precisely typed code optimises better, and the callee stops casting.

### Negative

- More small types to define, name and include, for values that were an `int`.
- Strong typing at a boundary can force conversions on either side of it, and
  conversion code is a place for new bugs.
- The type system cannot capture everything: which `Point` is the top-left
  remains convention no matter how strongly typed the interface is.

### Neutral

- This record does not mandate any particular units library beyond preferring
  `std::chrono` where durations are involved.
- Concept-constrained template parameters are the generic form of this rule;
  see [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md).

## Enforcement

- (Simple) Report the use of `void*` as a parameter or return type.
- (Simple) Report the use of more than one `bool` parameter.
- (Hard to do well) Look for functions that use too many primitive type
  arguments.

## Related

- [0007](0007-p-1-express-ideas-directly-in-code.md) — `change_speed(Speed)` is
  the same argument from the philosophy section.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — bundling
  parameters into a type fixes both problems at once.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — distinct types also stop adjacent arguments being swapped.
