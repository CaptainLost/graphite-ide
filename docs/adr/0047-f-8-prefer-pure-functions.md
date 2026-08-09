# 0047. F.8: Prefer pure functions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

One of the shortest rules in the guidelines — two sentences, one example, and
an admission that it cannot be enforced. It is transcribed for completeness and
because it names the property that makes `Core/Text` and `Core/Sln` testable.

## Decision

Adopt F.8 as written. Transcribed in full:

> **Reason.** Pure functions are easier to reason about, sometimes easier to
> optimize (and even parallelize), and sometimes can be memoized.

**Example.**

```cpp
template<class T>
auto square(T t) { return t * t; }
```

**Enforcement.** Not possible.

## Rationale

Adopted as written. Its practical value in this repository is as an explanation
of why the test suite is shaped the way it is: `TextEdit`, `SlnxEditor`'s
splicing helpers and the `.vcxproj`/`.csproj` readers take text in and give a
result out, touching no global state, which is exactly why they can be tested
byte for byte without a filesystem.

The rule is not extended into a preference for purity everywhere. This is a
program whose entire job is effects — reading a directory, watching for changes,
drawing a frame. What the rule usefully asks is that the *decision* be separable
from the effect: work out what to write, then write it. That separation is what
`ChangeCollector` does, and it is the form F.8 takes here.

## Consequences

### Positive

- Names the property the parsing and editing tests depend on, so it is defended
  as a design property rather than an accident.
- Suggests a concrete shape for new code: compute a result, then apply it.

### Negative

- Purity has a cost when the computed result is large — deciding what to change
  and then changing it can mean materialising an intermediate the effectful
  version never needed.
- The rule cannot be checked, and "prefer" gives it no force against a function
  that reads a member.

### Neutral

- No inventory. Classifying every function as pure or not would be a large
  manual exercise for a rule with no enforcement.

## Related

- [0046](0046-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md)
  — the previous rule.
- [0048](0048-f-9-unused-parameters-should-be-unnamed.md) — the next rule.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — P.10, the same
  preference applied to data.
