# 0384. SF.20: Use namespaces to express logical structure

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SF (Source files), transcribed 2026-08-10

## Context

Upstream supplies a title only. The rule's Reason, Example and Enforcement
sections are all empty placeholders, so there is no reasoning, no code, and no
check to transcribe.

## Decision

Use namespaces to express the logical structure of the code.

- Give each logically distinct component its own namespace, and nest namespaces
  to reflect the structure the components actually have.
- Do not use a namespace for something that is not a logical grouping — file
  layout, or the avoidance of a name clash between two things that belong
  together.

## Rationale

A namespace is the language's mechanism for grouping names, so if the grouping in
the code does not match the grouping in the design, the language is being used to
say something untrue.

## Consequences

### Positive

- The structure of the system is visible in qualified names at every use site.
- Component boundaries have a spelling, which makes them arguable.

### Negative

- The record rests on a title alone. There is no upstream reasoning to check it
  against, so anything beyond the sentence in the title is inference.
- "Logical structure" is undefined here, and the rule gives no test for when a
  grouping deserves a namespace.
- Deep nesting makes qualified names long, which pushes towards the `using`
  directives that
  [0377](0377-sf-6-use-using-namespace-directives-for-transition-for-foundation-libraries-such-as-std-or-within-a-local-scope-only.md)
  restricts.

### Neutral

- The rule says nothing about whether namespaces should mirror directories.

## Related

- [0377](0377-sf-6-use-using-namespace-directives-for-transition-for-foundation-libraries-such-as-std-or-within-a-local-scope-only.md)
  — when a namespace may be flattened at a use site.
- [0386](0386-sf-22-use-an-unnamed-anonymous-namespace-for-all-internal-non-exported-entities.md)
  — the namespace used for what is deliberately outside the structure.
