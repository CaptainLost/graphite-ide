# 0335. T.43: Prefer using over typedef for defining aliases

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Three reasons are given. Readability: with `using`, the new name comes first
rather than being embedded somewhere in a declaration. Generality: `using` can be
used for template aliases, whereas `typedef`s cannot easily be templates.
Uniformity: `using` is syntactically similar to `auto`.

```cpp
typedef int (*PFI)(int);   // OK, but convoluted
using PFI2 = int (*)(int);   // OK, preferred

template<typename T>
typedef int (*PFT)(T);      // error

template<typename T>
using PFT2 = int (*)(T);   // OK
```

## Decision

Write aliases with `using`.

- Declare every new type alias as `using Name = Type;`.
- Do not add a new `typedef`, including inside class definitions where the habit
  is strongest.
- Where the alias needs a template parameter, `using` is the only option, so a
  codebase that mixes the two ends up converting anyway.
- Leave existing `typedef`s alone unless the surrounding code is being changed;
  this is a rule about what is written next, not a licence for a sweeping edit.

## Rationale

`typedef` inherits declarator syntax, so the name being introduced appears
wherever a variable of that type would appear — buried in the middle for function
pointers and arrays — and the reader has to parse the declaration to find out
what is being defined. `using` puts the name on the left of an `=` in every case,
which is the same shape as every other definition in the language, and it is the
only form that can take template parameters, so it is a strict superset rather
than a matter of taste.

## Consequences

### Positive

- The name being introduced is always in the same place, so an alias can be read
  without parsing it.
- One syntax covers plain aliases and alias templates, which
  [0334](0334-t-42-use-template-aliases-to-simplify-notation-and-hide-implementation-details.md)
  relies on.
- Function pointer and array aliases become legible instead of convoluted.

### Negative

- Enforcement produces a very large number of hits, as upstream notes: `typedef`
  is pervasive in existing code and in every C header, so any check needs
  exemptions to be usable.
- Mixing the two forms during a gradual migration is itself an inconsistency, and
  the rule offers no completion criterion.
- Code that must also compile as C, or that is copied from a C header, cannot use
  `using` at all.
- The change is purely notational, so it competes for review attention with
  defects that matter more.

### Neutral

- The two forms are exactly equivalent for non-template aliases; nothing about
  the resulting type differs.
- Whether an alias should exist at all is
  [0334](0334-t-42-use-template-aliases-to-simplify-notation-and-hide-implementation-details.md)'s
  question.

## Enforcement

- Flag uses of `typedef`. This will produce a great many hits.

## Related

- [0334](0334-t-42-use-template-aliases-to-simplify-notation-and-hide-implementation-details.md)
  — alias templates, which require `using`.
- [0009](0009-p-3-express-intent.md) — the readability argument.
- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the `name = type` shape `using` matches.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  where C-compatible declarations, and hence `typedef`, remain necessary.
