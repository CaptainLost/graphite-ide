# 0437. NL.26: Use conventional const notation

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Conventional notation is more familiar to more programmers, and in a large code
base consistency of notation is itself worth something. The convention is to
write `const` before the type it qualifies.

```cpp
const int x = 7;    // OK
int const y = 9;    // bad
const int *const p = nullptr;   // OK, constant pointer to constant int
int const *const p = nullptr;   // bad, constant pointer to constant int
```

The guidelines are candid about the weakness of the argument: they are well
aware that the "bad" examples can be claimed to be more logical than the ones
marked "OK", because writing the qualifier to the right of what it qualifies is
uniform, whereas the conventional form is uniform only when the qualifier comes
first. Their answer is that the logical form confuses more people, especially
novices working from teaching material written in the far more common
conventional style. As ever, the stated aim of these naming and layout rules is
consistency, and aesthetics vary immensely. Upstream marks this as a
recommendation for when there are no constraints or better ideas, added after
many requests for guidance.

## Decision

Write `const` to the left of the type it qualifies.

- Spell a constant object `const T x`, not `T const x`.
- Spell a pointer to constant `const T* p`, and a constant pointer to constant
  `const T* const p`.
- Keep the trailing `const` on a member function, which qualifies the implicit
  object parameter and has no left-hand spelling.
- Follow the notation already used by surrounding code when working in it; the
  rule exists to make a file uniform, not to justify a rewrite.

## Rationale

Neither notation is clearer in the abstract, and the guidelines do not pretend
otherwise — they choose the more common one because familiarity is the only
tiebreaker available. What makes the choice worth stating at all is that a
mixture is genuinely worse than either: a reader who meets both spellings in
one file has to decide, each time, whether the difference is meant to signify
something.

## Consequences

### Positive

- Declarations match the form used in most teaching material and most existing
  code, so they read without translation.
- A single notation in a file removes the question of whether a differently
  placed `const` is deliberate.
- The convention is trivially learnable and requires no understanding of the
  declarator grammar to apply.

### Negative

- The chosen form is the less regular one, as upstream concedes. `const` binds
  to what is on its left except when there is nothing on its left, so the rule
  demands a special case in the reader's head.
- It cannot be applied uniformly. `T* const p` has no left-hand spelling, so a
  declaration with two qualifiers necessarily uses both orders, and the style
  is internally inconsistent at exactly the point where declarations are
  hardest to read.
- It interacts badly with type aliases and template parameters, where `const T`
  qualifies the alias rather than what the alias points to; the right-hand form
  makes that no better, but the left-hand form makes it look wrong.
- Enforcement is a pattern match on a suffix and cannot distinguish the member
  function `const` from a mis-placed qualifier without parsing, so the check
  either misses cases or produces noise.

### Neutral

- The two forms mean the same thing; nothing here changes what is const.
- Whether an object should be const at all is a separate decision, taken
  elsewhere.

## Enforcement

Flag `const` used as a suffix for a type.

## Related

- [0432](0432-nl-18-use-c-style-declarator-layout.md) — the other notational
  question inside a declaration, decided on the same grounds.
- [0425](0425-nl-8-use-a-consistent-naming-style.md) — the section's recurring
  argument that consistency outranks the particular choice.
- [0311](0311-con-1-by-default-make-objects-immutable.md) — the rule that
  decides when `const` appears at all, which this one only spells.
- [0313](0313-con-3-by-default-pass-pointers-and-references-to-consts.md) — the
  declarations where the qualifier turns up most often in practice.
