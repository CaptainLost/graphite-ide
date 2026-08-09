# 0033. I.22: Avoid complex initialization of global objects

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The rule numbering jumps from I.13 to I.22 upstream; the gap is in the source
document and is preserved here.

This codebase has two namespace-scope objects, both in
`Core/src/Core/Gui/ImGuiRenderer.cpp`, and both are raw pointers initialized to
null and assigned during startup — which is the shape this rule prefers.

## Decision

Adopt I.22 as written. Transcribed in full:

> **Reason.** Complex initialization can lead to undefined order of execution.

**Example.**

```cpp
// file1.c

extern const X x;

const Y y = f(x);   // read x; write y

// file2.c

extern const Y y;

const X x = g(y);   // read y; write x
```

Since `x` and `y` are in different translation units the order of calls to `f()`
and `g()` is undefined; one will access an uninitialized `const`. This shows
that the order-of-initialization problem for global (namespace scope) objects is
not limited to global variables.

**Note.** Order of initialization problems become particularly difficult to
handle in concurrent code. It is usually best to avoid global (namespace scope)
objects altogether.

**Enforcement.**

- Flag initializers of globals that call non-`constexpr` functions
- Flag initializers of globals that access `extern` objects

## Rationale

Adopted as written, and currently satisfied — which is worth recording, because
the natural "improvement" to the globals named in
[0021](0021-i-2-avoid-non-const-global-variables.md) would break it. Wrapping
`g_monoFont` in an object constructed at namespace scope would trade a rule this
project passes for one it does not.

Both enforcement points are checkable by eye and both pass: neither global has an
initializer that calls anything or reads an `extern`.

The Note's advice — avoid namespace-scope objects altogether — is the stronger
form, and is the subject of I.2 rather than this rule.

## Consequences

### Positive

- The current shape is not accidental and is now recorded as the one to keep:
  namespace-scope state, if any, is a null pointer assigned during a controlled
  startup, never an object with a constructor.
- The static initialization order fiasco cannot occur in this codebase as it
  stands.

### Negative

- Assign-during-startup means there is a window in which the globals are null,
  and nothing prevents a read in that window. The rule trades an ordering
  problem for a sequencing one.

### Neutral

- Vendored dependencies may have their own namespace-scope initialization; this
  record governs Core and Graphite only.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the previous
  rule.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the next rule.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the globals this rule
  constrains.
