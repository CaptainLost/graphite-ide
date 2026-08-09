# 0048. F.9: Unused parameters should be unnamed

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

This codebase suppresses unused-parameter warnings with `(void)name;` casts, not
by leaving parameters unnamed. There are five such casts —
`Layer::OnUpdate`, `DirectoryWatcher` twice, `FileDialog` twice — and no use of
`[[maybe_unused]]` anywhere.

The suppression is not optional: Core and Graphite build with `warnings "High"`
and `fatalwarnings { "All" }`, so an unused named parameter fails the build.

## Decision

Adopt F.9 as written. Transcribed in full:

> **Reason.** Readability. Suppression of unused parameter warnings.

**Example.**

```cpp
widget* find(const set<widget>& s, const widget& w, Hint);   // once upon a time, a hint was used
```

**Note.** Allowing parameters to be unnamed was introduced in the early 1980s to
address this problem.

If parameters are conditionally unused, declare them with the
`[[maybe_unused]]` attribute. For example:

```cpp
template <typename Value>
Value* find(const set<Value>& s, const Value& v, [[maybe_unused]] Hint h)
{
    if constexpr (sizeof(Value) > CacheSize)
    {
        // a hint is used only if Value is of a certain size
    }
}
```

**Enforcement.** Flag named unused parameters.

## Decision detail

An unconditionally unused parameter is left unnamed in the definition, with the
name kept in the declaration where it documents the interface:

```cpp
void OnUpdate(float /*deltaSeconds*/) override { }
```

A parameter unused only under some compile-time condition gets
`[[maybe_unused]]`. The `(void)name;` cast is not used in new code.

## Rationale

Adopted as written. The `(void)` cast achieves the same warning suppression, but
it does so in the function body, three lines away from the parameter list, and it
reads as a statement that does something. Leaving the parameter unnamed says the
same thing where the reader is already looking, and the commented-out name keeps
the documentation.

`[[maybe_unused]]` is the right tool for the conditional case and is available
under C++20 — see [0005](0005-target-c-20-with-headers-not-modules.md). None of
the five current sites is conditional.

The five existing casts are recorded rather than swept: each is a one-line change
and can be made when the surrounding code is next touched, exactly as with
[0006](0006-require-braces-around-every-control-flow-body.md).

## Consequences

### Positive

- The signature alone shows the parameter is ignored, without reading the body.
- One mechanically flaggable rule with a small, complete inventory.

### Negative

- An unnamed parameter cannot be referred to if the function later starts using
  it, so the name has to be restored — a trivial edit, but it means the two
  forms are not interchangeable at a glance.
- Leaving the name in a comment is a convention nothing enforces; it will drift
  from the declaration.

### Neutral

- The rule interacts with `fatalwarnings`: without warnings-as-errors, unused
  parameters would be a quiet nuisance rather than a build failure, and this
  rule would be cosmetic.

## Deviations to fix

| What | Where |
|---|---|
| `(void)deltaSeconds;` instead of an unnamed parameter | `Core/src/Core/App/Layer.h:19` |
| `(void)root;` | `Core/src/Core/Fs/DirectoryWatcher.cpp:144` |
| `(void)path;` | `Core/src/Core/Fs/DirectoryWatcher.cpp:501` |
| `(void)title;` | `Core/src/Core/Platform/FileDialog.cpp:127` |
| `(void)title;` | `Core/src/Core/Platform/FileDialog.cpp:227` |

## Related

- [0047](0047-f-8-prefer-pure-functions.md) — the previous rule.
- [0049](0049-f-10-if-an-operation-can-be-reused-give-it-a-name.md) — the next
  rule.
- [0005](0005-target-c-20-with-headers-not-modules.md) — why `[[maybe_unused]]`
  is available.
