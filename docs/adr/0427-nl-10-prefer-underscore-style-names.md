# 0427. NL.10: Prefer underscore_style names

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

The use of underscores to separate the parts of a name is the original C and C++
style, and it is the style of the C++ Standard Library. `my_map`, `string_view`
and `size_t` are all of a piece with the language they are written in, and a
name in the same style sits among them without announcing itself.

The rule is unusually hedged, and the hedges are part of it. It is a default to
use only when there is a choice; often there is no choice, and an established
style must be followed for consistency's sake, because the need for consistency
beats personal taste. It is a recommendation for when there are no constraints
or better ideas, and it was added after many requests for guidance rather than
because the authors wished to legislate. The variant offered is the ISO Standard
style with upper case for one's own types and concepts:

```cpp
int
vector
My_map
```

Enforcement is impossible.

## Decision

Use `underscore_style` names where the choice is genuinely open.

- When starting new code with no established convention, separate the words of
  an identifier with underscores and keep the rest in lower case.
- Optionally capitalise the first letter of your own types and concepts, in
  Stroustrup's variant, to distinguish them from standard-library types.
- Where a convention is already established — in the surrounding code, in a
  library being extended, in a project's written style — follow it instead. The
  established style wins over this default every time.
- Do not restyle existing code to match this rule. Consistency is the goal the
  rule serves, and a partial rename defeats it.

## Rationale

The argument for the underscore style is not that it is more legible in the
abstract but that it is the style of the language's own library, so code written
in it needs no mental gear change at the boundary between what you wrote and
what you called. That is a weak argument, and the rule knows it — which is why
it concedes in advance to any convention already in place.

## Consequences

### Positive

- Code in this style is typographically continuous with the standard library it
  calls.
- Having a stated default removes a decision from the start of a new project,
  which is when nobody has grounds to make it well.
- The concession to established styles means the rule never forces churn.

### Negative

- The rule very nearly cancels itself. Any codebase old enough to have a
  convention is exempt, so it applies almost exclusively to code that does not
  exist yet.
- Enforcement is impossible, by the guidelines' own admission, so it cannot be
  checked even where it does apply.
- It conflicts directly with the CamelCase conventions widespread in application
  and framework code, and a project that adopts it while depending on such
  libraries gets mixed conventions in every file anyway.
- Underscore names are longer than their CamelCase equivalents, which pushes
  against keeping declarations on one line.

### Neutral

- Macro names are unaffected: they are ALL_CAPS in every one of these styles,
  per [0426](0426-nl-9-use-all-caps-for-macro-names-only.md).
- The rule is about spelling alone and says nothing about what a name should
  mean or how long it should be.

## Enforcement

Impossible.

## Related

- [0425](0425-nl-8-use-a-consistent-naming-style.md) — the rule this one defers
  to whenever a style is already established.
- [0426](0426-nl-9-use-all-caps-for-macro-names-only.md) — the naming
  convention that is not negotiable.
- [0423](0423-nl-5-avoid-encoding-type-information-in-names.md) — a naming rule
  that applies whichever spelling is chosen.
- [0431](0431-nl-17-use-k-r-derived-layout.md) — the layout suggestion offered
  in the same spirit, and with the same hedging.
