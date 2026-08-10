# 0346. T.67: Use specialization to provide alternative implementations for irregular types

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Upstream supplies a title for this rule and nothing else: its reason, example
and enforcement sections are all placeholders. There is no reasoning to
transcribe, so this record records the rule itself and stops there.

## Decision

Where a type does not behave like the types the primary template was written
for — it is not copyable, not default-constructible, not regular in whatever
sense the template assumes — provide a specialization for it rather than
weakening or complicating the primary template.

## Rationale

The general implementation is written against a set of assumptions; a type that
breaks them needs different code, not a more defensive version of the same
code. Confining the irregularity to a specialization keeps its cost off every
other instantiation.

## Consequences

### Positive

- The primary template keeps the assumptions it was designed around.
- Support for an awkward type can be added without touching working code.

### Negative

- This record rests on a title alone. Upstream gives no reason, example or
  enforcement, so nothing here can be checked against the guidelines'
  reasoning, and the term "irregular" is left undefined.
- Its relationship to the general specialization rule is therefore unclear:
  read literally, the two rules say the same thing with different motivations.

### Neutral

- Which types count as irregular depends on what the primary template requires,
  so the rule can only be applied template by template.

## Related

- [0344](0344-t-64-use-specialization-to-provide-alternative-implementations-of-class-templates.md)
  — the general form of the same technique, which does carry a stated reason.
- [0049](0049-c-11-make-concrete-types-regular.md) — what regularity means, and
  therefore what a template is entitled to assume.
- [0367](0367-t-144-dont-specialize-function-templates.md) — the constraint on
  how the specialization must be written when the alternative is a function.
