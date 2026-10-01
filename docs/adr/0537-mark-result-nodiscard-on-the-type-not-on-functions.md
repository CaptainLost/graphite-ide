# 0537. Mark Result nodiscard on the type, not on functions

Status: accepted
Date: 2026-10-01

## Context

[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
rests on `Result` being `[[nodiscard]]`: with warnings-as-errors, a call that
drops a failure on the floor does not build. The house style in
[0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md) then
removed `[[nodiscard]]` from every function, and the attribute on `Result` went
with it, so an ignored `Result` compiled without a word. Restoring the attribute
found no call that silently dropped one - but six calls cast one to `void`,
and only one of them said why.

## Decision

- Mark the `Result` class template, and its `Result<void>` specialisation,
  `[[nodiscard]]`. Every function returning a `Result` inherits it; no function
  carries the attribute itself.
- Handle a returned failure: return it, log it, or show it.
- Where dropping one is genuinely correct, discard it with
  `static_cast<void>(...)` and a one-line comment saying why the failure does not
  matter there, or where it is already reported.

## Rationale

One attribute on the type gives 0004 its guarantee at every call site without
reintroducing the per-function decoration 0479 cut. A discard that has to state
its reason is a review prompt rather than a habit.

## Consequences

### Positive

- An ignored failure is a build break again in Core, Graphite and both test
  projects.
- A new function returning `Result` is covered without anyone remembering to
  mark it.

### Negative

- A call whose failure really does not matter now needs a cast and a comment;
  a `Result` returned purely for symmetry costs a line at each such caller.

### Neutral

- This amends 0004, whose promise it restores, and 0479, whose "no
  `[[nodiscard]]`" rule now means no `[[nodiscard]]` on functions.

## Alternatives Considered

- **Per-function `[[nodiscard]]`.** It is what 0479 removed, it must be
  remembered on every new declaration, and it covers nothing the type-level
  attribute does not.
- **Leave it off and rely on review.** That is the state that let 0004's
  guarantee lapse unnoticed.

## Related

- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md)
- [0479](0479-settle-one-house-style-for-naming-formatting-and-layout.md)
