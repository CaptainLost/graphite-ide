# 0194. ES.32: Use ALL_CAPS for all macro names

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The reasons are convention, readability, and distinguishing macros from
everything else.

```cpp
#define forever for (;;)   /* very BAD */
#define FOREVER for (;;)   /* Still evil, but at least visible to humans */
```

The upstream comments are the argument in miniature. Neither macro should exist
— [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) rules out
both — but between the two, the capitalised one at least announces itself. A
reader encountering `forever` in a function has no reason to suspect the
preprocessor; a reader encountering `FOREVER` does.

This rule is the converse of
[0179](0179-es-9-avoid-all-caps-names.md), and neither is much use without the
other. Together they make capitalisation a reliable signal: an ALL_CAPS
identifier may be a macro, and an identifier that is not ALL_CAPS is not one.

## Decision

Name every macro in ALL_CAPS.

- Give every `#define` — object-like or function-like, including include guards
  and configuration switches — an ALL_CAPS name.
- Do not define a macro whose name could plausibly be an ordinary identifier.
- Rename any lower-case or mixed-case macro that is under your own control.
- Do not extend the convention to non-macros; that is
  [0179](0179-es-9-avoid-all-caps-names.md).
- Combine this with
  [0195](0195-es-33-if-you-must-use-macros-give-them-unique-names.md): the name
  should be capitalised *and* prefixed.

## Rationale

The value of a naming convention is exactly its reliability, and this is the one
convention where the cost of an exception is paid by someone who has no way of
seeing it. A lower-case macro can rewrite any identifier in any translation unit
that transitively includes its header, and the resulting diagnostic points at
the victim's code. Capitalising every macro does not make macros good; it makes
them visible, which is all a naming rule can do.

## Consequences

### Positive

- A reader can tell from an identifier's appearance whether the preprocessor
  might be involved.
- Accidental capture of an ordinary identifier by a macro becomes far less
  likely, since ordinary identifiers are not capitalised.
- The convention is near-universal in C and C++, so it needs no explanation to
  anyone arriving at the codebase.

### Negative

- It only governs macros you define. Third-party and system headers define
  whatever they like, and lower-case macros in them are unaffected by any
  convention adopted elsewhere.
- Capitalisation makes macros conspicuous, which can read as emphasis and lend
  a macro-based facility more prominence in the code than it deserves.
- The rule makes macros comfortable to keep by making them survivable, which
  works mildly against
  [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) and
  [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md), whose
  position is that the macro should not exist.
- ALL_CAPS names collide with each other readily across libraries, which is why
  this rule alone is insufficient.

### Neutral

- The rule says nothing about which macros may exist; that is decided by
  [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) and
  [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md).
- Underscore separation within the name is implied by convention but not
  specified here.

## Enforcement

Scream when you see a lower-case macro.

## Related

- [0179](0179-es-9-avoid-all-caps-names.md) — the converse rule; the pair is what
  makes capitalisation informative.
- [0192](0192-es-30-dont-use-macros-for-program-text-manipulation.md) — most
  macros this rule would name should not exist at all.
- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — the other
  category of macro this applies to.
- [0195](0195-es-33-if-you-must-use-macros-give-them-unique-names.md) — the
  second half of naming a macro that must exist.
