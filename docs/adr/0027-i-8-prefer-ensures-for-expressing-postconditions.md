# 0027. I.8: Prefer Ensures() for expressing postconditions

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The postcondition counterpart to I.6. As with `Expects()`, `Ensures()` comes
from the GSL and is not available here.

## Decision

Adopt I.8 in substance, with `CORE_ASSERT` in place of `Ensures()` where a
postcondition is worth checking, and RAII in preference to either. Transcribed
in full:

> **Reason.** To make it clear that the condition is a postcondition and to
> enable tool use.

**Example.**

```cpp
void f()
{
    char buffer[MAX];
    // ...
    memset(buffer, 0, MAX);
    Ensures(buffer[0] == 0);
}
```

**Note.** Postconditions can be stated in many ways, including comments,
if-statements, and `assert()`. This can make them hard to distinguish from
ordinary code, hard to update, hard to manipulate by tools, and might have the
wrong semantics.

Alternative: Postconditions of the form "this resource must be released" are
best expressed by RAII.

**Note.** Ideally, that `Ensures` should be part of the interface, but that's not
easily done. For now, we place it in the definition (function body). Once
language support becomes available (e.g., see the contract proposal) we will
adopt the standard version of preconditions, postconditions, and assertions.

**Enforcement.** (Not enforceable) Finding the variety of ways postconditions
can be asserted is not feasible. Warning about those that can be easily
identified (`assert()`) has questionable value in the absence of a language
facility.

## Rationale

Adopted in substance. The rule's Alternative is the operative sentence for this
codebase: nearly every postcondition worth stating here is of the form "this
resource must be released", and RAII expresses those in code rather than in a
check. `Repository`, `DirectoryWatcher` and the file streams already do this.

Where a postcondition is a value property rather than a resource — a spliced
`.slnx` still parsing, a line index still consistent with its buffer — the
honest place for it in this project is a test, not an assert. The `SlnxEditor`
tests comparing edited files byte for byte are that postcondition, checked in
CI-visible form rather than in Debug-only code.

## Consequences

### Positive

- Points the common case at RAII, which is checkable by the compiler rather than
  at run time.
- Names tests as the substitute for `Ensures()` on value postconditions, which
  is where this project already puts its confidence.

### Negative

- Same weakness as [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md):
  `CORE_ASSERT` vanishes outside Debug and no tool reads it, so "enable tool
  use" is unmet.
- A postcondition expressed only as a test is checked for the cases the test
  covers and nowhere else.

### Neutral

- No `Ensures()`-style checks exist in the codebase today, and this record does
  not require adding any; it says which form to reach for when one is wanted.

## Related

- [0026](0026-i-7-state-postconditions.md) — the previous rule.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the next rule.
- [0014](0014-p-8-do-not-leak-any-resources.md) — RAII, the preferred form.
