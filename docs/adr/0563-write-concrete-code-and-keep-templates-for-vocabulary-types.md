# 0563. Write concrete code and keep templates for vocabulary types

Status: accepted
Date: 2026-10-05

## Context

The transcribed template records —
[0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md),
[0320](0320-t-5-combine-generic-and-oo-techniques-to-amplify-their-strengths-not-their-costs.md),
[0332](0332-t-40-use-function-objects-to-pass-operations-to-algorithms.md),
[0340](0340-t-49-where-possible-avoid-type-erasure.md) — encourage generic code
and discourage `std::function`. The codebase has few templates, but the survey
behind [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) found
about forty that a junior engineer has to decode for no gain: templates whose
only purpose is to accept a callable, templates that exist to carry an array's
length, and `requires`-expressions inside `if constexpr` used to detect what a
type can do. It also found templates that earn their place: `Result`, `Scope`,
`Ref`, `Out`, `Narrow`, `ScopeExit`, `GenerationSlot`, `LruResultCache`,
`ModalQuestion<Payload>`, `StackedTops`, the `NameOf` tables.

## Decision

Write concrete code. Keep a template only for a vocabulary type — a type that is
generic by nature and used with several unrelated arguments, like those above.

- A template whose only purpose is to accept a callable takes a
  `std::function` instead, unless it is on a hot path, where it takes the
  concrete function type it is actually used with.
- A template that only carries an array's length takes a `std::span`.
- Detection of a type's capabilities with `requires` inside `if constexpr`
  becomes overloads or concrete code for the types actually used.
- A new template needs a second, unrelated use before it is written; until then
  the concrete version is the code.

## Rationale

A template asks its reader to think about every type it could be instantiated
with, when the code only ever uses one or two. Concrete code asks only about the
types it names. `std::function`'s cost — an indirect call, and possibly an
allocation — is irrelevant off the hot path, and the hot paths are named in
[0558](0558-write-code-a-junior-engineer-can-read-like-prose.md).

## Consequences

### Positive

- Signatures name real types, so a call can be understood without working out
  what a template parameter will deduce to.
- Compiler errors point at the code, not into template instantiations.

### Negative

- A `std::function` parameter costs an indirect call and may allocate; on a path
  that turns out to be hot, that has to be measured and undone.
- Two near-identical concrete functions sometimes replace one template; the
  duplication is accepted until a second real use justifies the template.

### Neutral

- The vocabulary templates listed above are unchanged.

## Related

- [0558](0558-write-code-a-junior-engineer-can-read-like-prose.md) — the
  principle this applies.
- Amends [0316](0316-t-1-use-templates-to-raise-the-level-of-abstraction-of-code.md),
  [0320](0320-t-5-combine-generic-and-oo-techniques-to-amplify-their-strengths-not-their-costs.md),
  [0332](0332-t-40-use-function-objects-to-pass-operations-to-algorithms.md) and
  [0340](0340-t-49-where-possible-avoid-type-erasure.md).
