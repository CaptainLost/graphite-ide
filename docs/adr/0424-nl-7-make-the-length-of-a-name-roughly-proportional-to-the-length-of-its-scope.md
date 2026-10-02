# 0424. NL.7: Make the length of a name roughly proportional to the length of its scope

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

The larger the scope, the greater the chance of confusion and of an unintended
name clash. A name that is unambiguous inside three lines is not unambiguous
across a translation unit, and a name that is unambiguous across a translation
unit does not have to be long enough to survive one.

```cpp
double sqrt(double x);   // return the square root of x; x must be non-negative
int length(const char* p);  // return the number of characters in a zero-terminated C-style string
int length_of_string(const char zero_terminated_array_of_char[])    // bad: verbose
int g;      // bad: global variable with a cryptic name
int open;   // bad: global variable with a short, popular name
```

The two bad globals fail in opposite directions and for the same reason. `g`
carries no information at a scope where the reader has no other source of it;
`open` carries too little uniqueness at a scope where a clash with a familiar
name is likely. Meanwhile `p` for a pointer and `x` for a floating-point value
are conventional and non-confusing inside a small scope, and lengthening them
would only add noise.

Upstream gives no usable enforcement for this rule.

## Decision

Scale a name to the scope it lives in.

- Use short, conventional names for short-lived locals: `i` for a loop index,
  `p` for a pointer, `x` for a value, where the whole scope fits on a screen.
- Lengthen a name as its scope widens: a member is named for its role in the
  class, a name at namespace scope is named so it still reads correctly at a
  call site far from its definition.
- Do not pad a name that is already clear. `length_of_string` says nothing
  `length` did not.
- Never give a wide-scope name a cryptic one-letter form, and never give one a
  short, popular word likely to be wanted elsewhere.

## Rationale

Name length is not a measure of quality, it is a budget spent against the
distance between a name's definition and its use. The reader of a three-line
loop has the definition in view and needs nothing from the name; the reader of a
call in another file has only the name, and it must carry the whole meaning
alone. Both bad globals above are the same error — a name priced for the wrong
distance.

## Consequences

### Positive

- Small scopes stay dense and readable, without ceremonial names for values
  that live for two lines.
- Wide-scope names carry enough meaning to be understood where they are used,
  which is usually far from where they are declared.
- Accidental clashes at namespace scope become less likely, because the names
  there are specific.

### Negative

- "Roughly proportional" is not a criterion anyone can apply identically twice,
  so the rule cannot settle a disagreement — it can only frame one.
- It quietly rewards keeping scopes small and can be satisfied by shrinking the
  scope instead of improving the name, which is sometimes the right move and
  sometimes an evasion.
- Refactoring changes a name's scope without changing the name. Lifting a local
  to a member leaves a name priced for the old scope, and nothing flags it.

### Neutral

- The rule is about length only. Which characters and which case are used is
  [0425](0425-nl-8-use-a-consistent-naming-style.md)'s business.
- The conventional short names — `i`, `p`, `x` — are conventional because they
  are widely understood, not because they are short; a novel one-letter name has
  none of the same licence.

## Related

- [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  — the same rule in the ES section, stated in terms of commonness as well as
  scope.
- [0175](0175-es-5-keep-scopes-small.md) — small scopes are what make short
  names legitimate.
- [0433](0433-nl-19-avoid-names-that-are-easily-misread.md) — short names are
  the ones most easily confused with each other.
- [0425](0425-nl-8-use-a-consistent-naming-style.md) — the style within which
  these lengths are chosen.
