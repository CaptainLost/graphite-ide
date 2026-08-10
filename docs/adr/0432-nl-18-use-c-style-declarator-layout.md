# 0432. NL.18: Use C++-style declarator layout

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

A declarator can be spaced to group with the type or to group with the name.
The C-style layout — `T *p`, `T &r` — emphasises use in expressions and follows
the grammar, in which the `*` belongs to the declarator rather than to the type.
The C++-style layout — `T* p`, `T& r` — emphasises the type: what is being
declared is a pointer to `T`, and the notation says so. The guidelines observe
that the use-in-expressions argument, whatever its force for pointers, does not
hold for references at all, since there is no prefix `&` operation on a
reference that recovers the referent.

```cpp
T& operator[](size_t);   // OK
T &operator[](size_t);   // just strange
T & operator[](size_t);  // undecided
```

Upstream marks this as a recommendation for when there are no constraints or
better ideas, added after many requests for guidance. Enforcement is described
as impossible in the face of history.

## Decision

Bind the declarator punctuation to the type, not to the name.

- Write `T* p` and `T& r`, with the `*` or `&` attached to the type and a space
  before the name.
- Apply the same spacing in parameter lists, return types and member
  declarations, so a signature reads as a list of types.
- Do not leave the punctuation floating between spaces on both sides; that
  commits to neither reading.
- Follow whatever layout the surrounding code already uses when working in it;
  this is a default for code that has no established convention.

## Rationale

The rule is a bet about how declarations are read rather than about how they
are parsed. Almost nobody reads `T* p` by re-deriving the grammar; they read a
type followed by a name, and the spacing that matches that reading is the one
that costs the least. The references argument is the part that actually
settles it — for references the C-style justification does not exist, so
adopting C-style spacing there is imitation rather than reasoning.

## Consequences

### Positive

- A declaration reads as type-then-name, matching how the rest of the language
  is written and how most programmers parse it.
- References and pointers are spaced the same way, so one habit covers both
  instead of two rules with different justifications.
- Function signatures line up as sequences of types, which makes overload sets
  easier to compare by eye.

### Negative

- The spacing tells a lie the grammar can still expose. `T* p, q` declares one
  pointer and one `T`, and the layout that makes `T*` look like a type is
  exactly what makes that outcome surprising. The style is only safe alongside
  the discipline of declaring one name per declaration.
- It is unenforceable by the guidelines' own admission, so a codebase that
  consumes headers written in either style ends up mixed regardless.
- Const-qualified pointer declarations do not become any clearer.
  `const T* const p` still has to be read right to left, and no spacing
  convention rescues it.
- The rule asks for churn-free adoption but gives no way to check compliance,
  so it survives only as long as everyone remembers it.

### Neutral

- Nothing here changes meaning; the two layouts parse identically.
- Which side `const` is written on is a separate question with its own rule.

## Enforcement

Impossible in the face of history: both layouts are widespread and both will
continue to arrive in headers you do not own.

## Related

- [0435](0435-nl-21-declare-one-name-only-per-declaration.md) — the discipline
  that makes this spacing safe rather than misleading.
- [0180](0180-es-10-declare-one-name-only-per-declaration.md) — the same
  requirement stated among the expression and statement rules, with reasoning.
- [0431](0431-nl-17-use-k-r-derived-layout.md) — the layout suggestion this one
  accompanies.
- [0437](0437-nl-26-use-conventional-const-notation.md) — the other spacing
  question in a declaration, decided the other way round.
