# 0409. A.1: Separate stable code from less stable code

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section A (Architectural ideas), transcribed 2026-08-10

## Context

Upstream supplies this rule as a title and a single sentence: isolating less
stable code facilitates its unit testing, interface improvement, refactoring,
and eventual deprecation. There is no Reason heading, no example, no note and no
enforcement, so there is nothing further to transcribe.

## Decision

Keep code that is expected to change apart from code that is not.

- Put the volatile part behind its own interface, in its own component, so that
  it can be tested, reshaped, replaced or removed without disturbing the stable
  part.
- Do not let stable code acquire dependencies on unstable code; the dependency
  runs the other way.

## Rationale

The four things the rule names — unit testing, interface improvement,
refactoring and deprecation — are all operations on a boundary. Code that has no
boundary around it cannot be subjected to any of them without touching
everything that touches it.

## Consequences

### Positive

- The unstable part can be replaced or deleted as a unit, because its extent is
  known.
- A boundary drawn for instability is also a seam for testing.

### Negative

- The record rests on a single upstream sentence, so it cannot be checked
  against upstream reasoning, and it says nothing about how the separation is
  achieved.
- Which code is "less stable" is a prediction, and a boundary drawn around the
  wrong prediction is pure overhead.
- Introducing a boundary costs an interface, an indirection, and the work of
  keeping both sides in step.

### Neutral

- The separation may be a namespace, a class, a translation unit or a library;
  the rule does not choose, and neither does this record.

## Related

- [0410](0410-a-2-express-potentially-reusable-parts-as-a-library.md) — the same
  separation, drawn at library granularity.
- [0042](0042-c-3-represent-the-distinction-between-an-interface-and-an-implementation-using-a-class.md)
  — the mechanism by which a volatile implementation is isolated.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — one way
  to keep a changing implementation from disturbing a stable interface.
