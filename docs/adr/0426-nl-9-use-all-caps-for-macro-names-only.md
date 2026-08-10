# 0426. NL.9: Use ALL_CAPS for macro names only

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

ALL_CAPS is reserved for macros so that a macro is never confused with a name
that obeys scope and type rules. A macro obeys neither: it is a textual
substitution that ignores namespaces, ignores classes, and applies from its
definition to the end of the translation unit. The visual distinction is the
reader's only warning.

```cpp
void f()
{
    const int SIZE{1000};  // Bad, use 'size' instead
    int v[SIZE];
}
```

The rule applies to non-macro symbolic constants generally, enumerators
included:

```cpp
enum bad { BAD, WORSE, HORRIBLE }; // BAD
```

An ALL_CAPS constant is also a standing hazard in a second way that follows from
the first: if any header anywhere defines a macro of the same name, the constant
is rewritten under the author's feet, and the error message will be about
whatever the macro expanded to.

## Decision

Reserve ALL_CAPS for macros, and use it for every macro.

- Name every macro in ALL_CAPS, and give it a project-specific prefix so that it
  cannot collide with a macro from elsewhere.
- Do not name a constant, an enumerator, a variable, a type or a function in
  ALL_CAPS. A `constexpr` constant takes a normal name — `size`, not `SIZE`.
- Prefer a `constexpr` constant or a scoped enumeration to a macro in the first
  place; this rule governs what is left after that choice.
- Read an ALL_CAPS identifier as a macro, and treat an ALL_CAPS non-macro as a
  defect to be renamed.

## Rationale

This is the one rule in a section of aesthetic suggestions that has a mechanical
consequence: a macro can silently capture any identifier spelled the same way,
and the ALL_CAPS convention exists so that the set of names at risk is
recognisable on sight. That makes the convention a safety property rather than a
preference — an ALL_CAPS constant is not ugly, it is exposed.

## Consequences

### Positive

- Every name that ignores scope looks different from every name that respects
  it.
- The set of identifiers a macro can capture is visible, so a collision is
  predictable rather than mysterious.
- Compliance is cheap to check by eye: every `#define` shouts, and nothing else
  does.

### Negative

- The convention protects less than it appears to. ALL_CAPS makes macros
  recognisable, but only a distinctive prefix actually prevents a collision with
  a macro from another library, and this rule does not require one.
- It cuts against decades of C practice in which constants shout, so imported
  code, examples and translated snippets arrive violating it, and each one has
  to be renamed on the way in.
- Nothing in the language enforces it, so it is upheld by convention and by an
  optional external checker; a single ALL_CAPS constant is enough to make the
  visual rule unreliable for a reader.

### Neutral

- Imported code follows its own conventions and is not restyled; its macros are
  outside the scope of a house rule.
- Which non-shouting spelling a constant uses is
  [0425](0425-nl-8-use-a-consistent-naming-style.md)'s business, not this rule's.

## Enforcement

- Flag macros containing lower-case letters.
- Flag ALL_CAPS names that are not macros.

## Related

- [0179](0179-es-9-avoid-all-caps-names.md) — the same prohibition stated from
  the non-macro side.
- [0194](0194-es-32-use-all-caps-for-all-macro-names.md) — the converse
  obligation: every macro shouts.
- [0143](0143-enum-5-dont-use-all-caps-for-enumerators.md) — enumerators are the
  most common violation of this rule.
- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — avoiding
  the macro entirely is better than naming it well.
