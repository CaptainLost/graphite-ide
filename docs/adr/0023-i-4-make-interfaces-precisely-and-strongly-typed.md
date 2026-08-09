# 0023. I.4: Make interfaces precisely and strongly typed

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

Three of this rule's targets are present in the codebase: `void*` parameters and
members in `EventBus`, `DirectoryWatcher` and `Window::NativeHandle`; a function
taking two adjacent `bool`s in `DocumentSet`; and integer parameters whose unit
is carried by a comment rather than a type.

## Decision

Adopt I.4 as written. Transcribed in full:

> **Reason.** Types are the simplest and best documentation, improve legibility
> due to their well-defined meaning, and are checked at compile time. Also,
> precisely typed code is often optimized better.

**Example, don't.** Consider:

```cpp
void pass(void* data);    // weak and under-qualified type void* is suspicious
```

Callers are unsure what types are allowed and if the data may be mutated as
`const` is not specified. Note all pointer types implicitly convert to `void*`,
so it is easy for callers to provide this value.

The callee must `static_cast` data to an unverified type to use it. That is
error-prone and verbose.

Only use `const void*` for passing in data in designs that are indescribable in
C++. Consider using a `variant` or a pointer to base instead.

Alternative: Often, a template parameter can eliminate the `void*` turning it
into a `T*` or `T&`. For generic code these `T`s can be general or concept
constrained template parameters.

**Example, bad.** Consider:

```cpp
draw_rect(100, 200, 100, 500); // what do the numbers specify?

draw_rect(p.x, p.y, 10, 20); // what units are 10 and 20 in?
```

It is clear that the caller is describing a rectangle, but it is unclear what
parts they relate to. Also, an `int` can carry arbitrary forms of information,
including values of many units, so we must guess about the meaning of the four
`int`s. Most likely, the first two are an x,y coordinate pair, but what are the
last two?

Comments and parameter names can help, but we could be explicit:

```cpp
void draw_rectangle(Point top_left, Point bottom_right);
void draw_rectangle(Point top_left, Size height_width);

draw_rectangle(p, Point{10, 20});  // two corners
draw_rectangle(p, Size{10, 20});   // one corner and a (height, width) pair
```

Obviously, we cannot catch all errors through the static type system (e.g., the
fact that a first argument is supposed to be a top-left point is left to
convention (naming and comments)).

**Example, bad.** Consider:

```cpp
set_settings(true, false, 42); // what do the numbers specify?
```

The parameter types and their values do not communicate what settings are being
specified or what those values mean.

This design is more explicit, safe and legible:

```cpp
alarm_settings s{};
s.enabled = true;
s.displayMode = alarm_settings::mode::spinning_light;
s.frequency = alarm_settings::every_10_seconds;
set_settings(s);
```

For the case of a set of boolean values consider using a flags enum; a pattern
that expresses a set of boolean values.

```cpp
enable_lamp_options(lamp_option::on | lamp_option::animate_state_transitions);
```

**Example, bad.** In the following example, it is not clear from the interface
what `time_to_blink` means: Seconds? Milliseconds?

```cpp
void blink_led(int time_to_blink) // bad -- the unit is ambiguous
{
    // ...
    // do something with time_to_blink
    // ...
}

void use()
{
    blink_led(2);
}
```

**Example, good.** `std::chrono::duration` types help making the unit of time
duration explicit.

```cpp
void blink_led(milliseconds time_to_blink) // good -- the unit is explicit
{
    // ...
    // do something with time_to_blink
    // ...
}

void use()
{
    blink_led(1500ms);
}
```

The function can also be written in such a way that it will accept any time
duration unit.

```cpp
template<class rep, class period>
void blink_led(duration<rep, period> time_to_blink) // good -- accepts any unit
{
    // assuming that millisecond is the smallest relevant unit
    auto milliseconds_to_blink = duration_cast<milliseconds>(time_to_blink);
    // ...
    // do something with milliseconds_to_blink
    // ...
}

void use()
{
    blink_led(2s);
    blink_led(1500ms);
}
```

**Enforcement.**

- (Simple) Report the use of `void*` as a parameter or return type.
- (Simple) Report the use of more than one `bool` parameter.
- (Hard to do well) Look for functions that use too many primitive type
  arguments.

## Rationale

Adopted as written. Its first two enforcement points are mechanical, and both
find something here — which is the useful kind of rule.

Not every `void*` in this codebase is a violation of the same weight. Three
distinct cases, and they deserve different verdicts:

- `EventBus`'s `const void*` payload is type erasure inside a template that
  immediately casts back to the type it erased. The rule's own text permits
  `const void*` "in designs that are indescribable in C++", and a typed event
  bus with a heterogeneous handler map is close to that. Accepted.
- `Window::NativeHandle() -> void*` is the deliberate seam that keeps
  `Windows.h` and GLFW out of the header, per
  [0002](0002-keep-core-free-of-ide-knowledge.md). Accepted, with the note that
  the alternative — an opaque handle type — would cost nothing and say more.
- `DirectoryWatcher`'s `void* m_directory` and `void* m_stopEvent` are Win32
  `HANDLE`s in disguise, cast at every use. These are the weakest of the three.

The `std::chrono` example applies directly: `DirectoryWatcher` already uses a
`kArmTimeout` duration rather than a bare integer, which is the rule followed.

## Consequences

### Positive

- Two mechanically checkable enforcement points, both of which found real
  instances here.
- Gives a written reason to prefer a small struct over a run of primitive
  parameters, which is the shape most likely to be miscalled.

### Negative

- Following it fully means new types — an opaque native handle, a
  settings struct, a flags enum — each of which is a header and a name to learn.
  At this codebase's size that overhead is not free.
- The `void*` in `EventBus` cannot be removed without giving up the
  heterogeneous handler map, so this rule identifies something it cannot fix.

### Neutral

- The rule's "too many primitive arguments" point overlaps
  [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) and
  [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md);
  the inventory is kept there.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| Two adjacent `bool` parameters | `Core/src/Core/Text/DocumentSet.h:156` — `Add(path, bool permanent, bool confirmed)` | Matches the second enforcement point exactly; a flags enum or two named types would fix it |
| Win32 `HANDLE`s held as `void*` and cast at each use | `Core/src/Core/Fs/DirectoryWatcher.h:130`, `:131` | Deliberate, to keep `Windows.h` out of the header; an opaque wrapper type would keep that and lose the casts |
| `void*` return in the platform seam | `Core/src/Core/Platform/Window.h:41`, `GlfwWindow.h:25` | Accepted as the boundary, but an opaque handle type would be stronger |

## Related

- [0022](0022-i-3-avoid-singletons.md) — the previous rule.
- [0024](0024-i-5-state-preconditions-if-any.md) — the next rule.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — the same `DocumentSet::Add` signature, from the swap-hazard angle.
