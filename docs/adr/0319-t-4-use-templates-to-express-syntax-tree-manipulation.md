# 0319. T.4: Use templates to express syntax tree manipulation

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

**This rule has no body.** Upstream supplies a title and nothing else: the
Reason, the Example and the Exceptions are all placeholders, so there is no
reasoning, no code, no note and no enforcement to transcribe. The rule's own
wording is the whole of the available substance.

## Decision

Use templates, rather than a run-time representation, to express manipulation of
a syntax tree.

- Take the title at face value where a syntax tree is built and transformed:
  templates are the mechanism the guidelines point at for it.
- Do not cite T.4 for anything beyond that sentence. With no reason, no example
  and no exception, it cannot settle a design argument either way.
- Judge a concrete expression-template design under the rules that do have
  content — constrain the parameters with meaningful concepts, keep the
  template's context dependencies down — and record that design as a decision of
  its own.

## Rationale

The title asserts that syntax-tree manipulation is among the things templates
are for, and upstream offers no argument for it, so this record asserts no more
than the title does.

## Consequences

### Positive

- The rule's number and title are on record, so the gap is visible rather than
  looking like an oversight.
- No reasoning is attributed to the guidelines that they do not contain.

### Negative

- The record rests on a title alone, so nothing in it can be checked against
  upstream reasoning, and it cannot say when the rule applies or what it costs.
- It offers a reader nothing beyond a restatement, so a link here returns little.
- Expression templates have real costs — diagnostics, dangling references to
  temporaries, build time — on which this record takes no position, leaving the
  topic ungoverned.

### Neutral

- Nothing is required to change, and nothing is prohibited.
- If a later revision of the guidelines fills T.4 in, this record can be
  superseded by one written from that text.

## Related

- [0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md),
  [0317](0317-t-2-use-templates-to-express-algorithms-that-apply-to-many-argument-types.md)
  and [0318](0318-t-3-use-templates-to-express-containers-and-ranges.md) — the
  three sibling rules that do have content.
- [0340](0340-t-49-where-possible-avoid-type-erasure.md) — the other rule in this
  section whose body was left largely unwritten.
