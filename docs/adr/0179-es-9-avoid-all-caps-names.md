# 0179. ES.9: Avoid ALL_CAPS names

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

ALL_CAPS names are commonly used for macros. A non-macro entity given an
ALL_CAPS name is therefore vulnerable to unintended macro substitution by any
header that happens to define that name — and the preprocessor obeys no scope
rules, so "any header" means any header included anywhere before the use.

```cpp
// somewhere in some header:
#define NE !=

// somewhere else in some other header:
enum Coord { N, NE, NW, S, SE, SW, E, W };

// somewhere third in some poor programmer's .cpp:
switch (direction) {
case N:
    // ...
case NE:
    // ...
// ...
}
```

The enumerator `NE` is replaced by `!=` before the compiler sees the enum at
all. The resulting diagnostic points at the enum or the `switch`, and mentions
neither the macro nor the header that defined it. Nobody involved in writing
either file did anything unusual.

A note worth stating separately: do not use ALL_CAPS for constants merely
because constants used to be macros. `constexpr` and `const` objects are
ordinary entities with ordinary scope, and there is no reason for them to
advertise themselves as preprocessor tokens.

## Decision

Reserve ALL_CAPS for macros, and use it for nothing else.

- Name constants, `constexpr` objects, enumerators, variables, functions, types
  and namespaces in whatever case convention ordinary names use, not in
  capitals.
- Do not translate a former `#define` into an ALL_CAPS `constexpr` when
  replacing it; rename it at the same time.
- Where a third-party header defines an ALL_CAPS macro that collides with an
  ordinary name, rename that entity or `#undef` the macro at the boundary; do
  not leave the collision in place.
- The corresponding positive obligation — that every macro that does get
  defined is ALL_CAPS — is
  [0194](0194-es-32-use-all-caps-for-all-macro-names.md).

## Rationale

This rule and its converse together buy one thing: the ability to look at an
identifier and know whether the preprocessor might rewrite it. That guarantee is
worth more than any argument about which case constants "should" have, because
it is the only defence available against a substitution that happens before
compilation and respects no scope. Note the asymmetry — the enum author in the
example cannot avoid the bug by being careful, only by not using capitals.

## Consequences

### Positive

- An ALL_CAPS identifier reads as "the preprocessor may be involved here", which
  is exactly the warning a reader needs.
- Non-macro entities stop colliding with macros from system and third-party
  headers, which is where most such collisions originate.
- Removing capitals from constants is a step towards their being ordinary
  typed, scoped objects rather than textual substitutions with extra steps.

### Negative

- The convention is widespread in the opposite direction: decades of C and C++
  code name constants in capitals, and readers arriving from that tradition will
  read a lower-case constant as a variable.
- It does not protect against a macro whose name is not in capitals, which
  [0194](0194-es-32-use-all-caps-for-all-macro-names.md) can only enforce for
  macros under one's own control, not for those arriving from third-party
  headers.
- Platform, system and graphics headers define large numbers of ALL_CAPS
  macros, so the collision risk is real and this rule reduces rather than
  removes it.

### Neutral

- Legacy code is treated differently: there, ALL_CAPS is accepted for macro
  names and the thing worth flagging is a macro whose name is *not* ALL_CAPS.
- The rule constrains case, not length or wording.

## Enforcement

Flag all uses of ALL_CAPS. For older code, accept ALL_CAPS for macro names and
flag all non-ALL-CAPS macro names.

## Related

- [0194](0194-es-32-use-all-caps-for-all-macro-names.md) — the converse rule;
  together the two make capitalisation a reliable signal.
- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — the reason a
  constant does not need to look like a macro.
- [0195](0195-es-33-if-you-must-use-macros-give-them-unique-names.md) — the other
  half of managing macro name collisions.
- [0178](0178-es-8-avoid-similar-looking-names.md) — another rule where a name's
  appearance must not mislead.
