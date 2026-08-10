# 0174. ES.3: Don't repeat yourself, avoid redundant code

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Duplicated or otherwise redundant code obscures intent, makes the logic harder
to follow, and makes maintenance harder. It most often arises from cut-and-paste
programming, where the second copy is created without a decision ever being made
to have two. Using a standard algorithm where one applies, instead of writing an
implementation of the same thing, is one instance of this rule.

```cpp
void func(bool flag)    // Bad, duplicated code.
{
    if (flag) {
        x();
        y();
    }
    else {
        x();
        z();
    }
}
```

```cpp
void func(bool flag)    // Better, no duplicated code.
{
    x();
    if (flag)
        y();
    else
        z();
}
```

The duplicated `x()` is small enough to look harmless, and that is exactly the
problem: nothing in the first version says that the two calls are meant to be
the same call. A later edit that changes one of them, or that inserts something
before one of them, does not read as a change in behaviour, and the fact that
`x()` is unconditional is no longer visible at a glance.

Note that the upstream "better" version writes its `if`/`else` bodies without
braces; nothing in the argument about duplication turns on that.

## Decision

Have one copy of each piece of logic, and make redundancy a thing that is chosen
rather than a thing that accumulates.

- Hoist statements that are common to every branch of a conditional out of the
  conditional, so that the branch contains only what actually differs.
- Call a standard algorithm rather than writing an equivalent loop.
- When the same sequence of operations appears in two places, factor it into a
  function; when it appears in two types, factor it into a base, a template, or
  a free function taking the common interface.
- Do not copy a block in order to change three lines of it. Parameterise the
  difference.

## Rationale

Redundancy is not primarily a cost in bytes; it is a cost in truth. Two copies
of a rule are two places the rule can be stated, and nothing in the language
keeps them agreeing. The interesting failure is not that the duplicate is
tedious to update but that a reader cannot tell whether the divergence between
two nearly identical blocks is deliberate or is the bug they are hunting.

## Consequences

### Positive

- A rule is changed in one place, so the change cannot be made half way.
- Branches shrink to just what differs, which is what a reader is trying to see.
- Extracting the common part often reveals that the abstraction was missing, not
  merely the function.

### Negative

- Deduplication can couple two call sites that were only coincidentally similar,
  and coincidental duplication is genuinely common; the wrong extraction is
  harder to undo than the duplication was to tolerate.
- Hoisting a call out of a conditional changes the order of operations relative
  to the condition's evaluation. That is usually harmless and occasionally not.
- The result is often a function with a flag parameter or several small helpers,
  which trades duplication for indirection.
- "Redundant" has no mechanical definition, so the rule cannot be fully
  enforced; static analysis catches only some shapes.

### Neutral

- Test code tolerates more repetition than production code, because explicitness
  at the assertion site is worth more there.
- The rule is about logic, not about text. Two similar-looking blocks that
  express unrelated rules are not duplicates.

## Enforcement

- Use a static analyser; it will catch at least some redundant constructs.
- Code review.

## Related

- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — a hand-written algorithm duplicates one the library already has.
- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the same principle applied to type names in a declaration.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the static analyser
  this rule's enforcement depends on.
