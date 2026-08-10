# 0202. ES.45: Avoid "magic constants"; use symbolic constants

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Unnamed constants embedded in expressions are easily overlooked and often hard
to understand:

```cpp
for (int m = 1; m <= 12; ++m)   // don't: magic constant 12
    cout << month[m] << '\n';
```

No, we do not all know that there are 12 months, numbered 1 to 12, in a year.
Naming the bounds says both the value and the convention:

```cpp
// months are indexed 1..12
constexpr int first_month = 1;
constexpr int last_month = 12;

for (int m = first_month; m <= last_month; ++m)   // better
    cout << month[m] << '\n';
```

Better still is not to expose the constants at all, because the container
already knows its own extent:

```cpp
for (auto m : month)
    cout << m << '\n';
```

## Decision

Give a literal a name when the literal carries a meaning, and prefer removing
the need for it altogether.

- Introduce a `constexpr` named constant for any literal whose value encodes a
  domain fact, a limit, a size, a conversion factor or a protocol detail.
- Name the constant after what it means, not after its value: `last_month`, not
  `twelve`.
- Where the constant exists only to bound an iteration over a container, delete
  it and iterate the container; see
  [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md).
- Where a set of related constants belongs together, prefer an enumeration; see
  [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md).
- Do not define the constant as a macro; see
  [0139](0139-enum-1-prefer-enumerations-over-macros.md).
- Leave the uninteresting literals alone: `0`, `1`, `nullptr`, `'\n'` and `""`
  are not magic.

## Rationale

The name is not primarily documentation, it is a single point of change. A
literal repeated at three sites is three places to edit and three chances to
edit only two, and nothing relates the three occurrences to one another —
grepping for `12` finds every unrelated twelve in the codebase.

The third example is the one worth remembering: the best treatment of a magic
constant is usually not to name it but to arrange for the code not to need it,
because a bound that is derived from the data cannot disagree with the data.

## Consequences

### Positive

- A value with a name can be found, changed in one place, and reasoned about at
  the point of definition where its units and convention can be stated.
- Related constants declared together make an inconsistency between them visible
  — `first_month` and `last_month` sit on adjacent lines.
- `constexpr` constants carry a type, so a named constant participates in
  overload resolution and type checking in a way a bare literal does not.

### Negative

- A name that merely restates the value adds a level of indirection and no
  information. `constexpr int twelve = 12;` is strictly worse than `12`.
- There is no objective test for which literals are magic. The upstream
  enforcement is "flag literals, with a positive list of exceptions", and the
  contents of that list are a judgement call that will differ between reviewers.
- A constant declared far from its single use site makes the code harder to
  read, not easier, because understanding one line now requires a jump.
- Naming a constant tempts people to widen its scope — a file-scope or
  header-scope constant used once becomes part of an interface — which trades a
  small readability gain for a real coupling cost.

### Neutral

- This record says nothing about how the constant is spelled beyond ruling out
  macros; in particular, an ALL_CAPS name is discouraged for a non-macro
  constant by
  [0179](0179-es-9-avoid-all-caps-names.md).

## Enforcement

Flag literals in code. Give a pass to `0`, `1`, `nullptr`, `\n`, `""`, and
others on a positive list.

## Related

- [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md)
  — where the constants form a set, an enumeration is the better container.
- [0139](0139-enum-1-prefer-enumerations-over-macros.md) — what not to use to
  give the constant a name.
- [0179](0179-es-9-avoid-all-caps-names.md) — how not to spell it.
- [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md)
  — the loop form that makes the bound unnecessary.
