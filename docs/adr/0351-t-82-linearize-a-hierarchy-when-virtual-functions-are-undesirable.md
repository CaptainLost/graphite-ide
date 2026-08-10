# 0351. T.82: Linearize a hierarchy when virtual functions are undesirable

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Upstream supplies a title for this rule and nothing else: its reason, example
and enforcement sections are placeholders. There is nothing to transcribe, so
this record states the rule and the little that follows from its own wording.

## Decision

Where virtual functions cannot be afforded — no vtable pointer in the object,
no indirect call on the hot path, no dependence on a polymorphic layout — do
not keep the hierarchy and merely avoid marking its functions virtual. Flatten
it: express the relationship between the types linearly, so that each layer is
resolved statically rather than by dynamic dispatch.

## Rationale

Virtual functions are the thing being removed, and a class hierarchy without
them is a hierarchy that has lost the mechanism it was built around. If the
dispatch must go, the structure that existed to support it should go with it,
rather than remaining as inheritance that no longer carries meaning.

## Consequences

### Positive

- The per-object vtable pointer and the indirect call disappear.
- What was a run-time choice becomes visible in the type, so the compiler can
  inline across it.

### Negative

- This record rests on a title alone. Upstream gives no reason, no example and
  no enforcement, so "linearize" is not defined here, and the record cannot be
  checked against the guidelines' reasoning.
- Nothing in it says what the linear form should be, which means it offers no
  guidance at the moment of doing the work.
- Removing dynamic dispatch removes run-time extensibility, which is usually
  why the hierarchy existed.

### Neutral

- The rule presupposes that virtual functions are undesirable; deciding that is
  a separate judgement and needs a measurement, not a preference.

## Related

- [0349](0349-t-80-do-not-naively-templatize-a-class-hierarchy.md) — the other
  rule about a hierarchy whose virtual functions have become a cost.
- [0352](0352-t-83-do-not-declare-a-member-function-template-virtual.md) — the
  case where the language, rather than the budget, forbids the virtual
  function.
- [0107](0107-c-132-dont-make-a-function-virtual-without-reason.md) — the prior
  question of whether the virtual functions should have been there.
