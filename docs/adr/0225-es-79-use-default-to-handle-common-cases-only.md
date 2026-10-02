# 0225. ES.79: Use default to handle common cases (only)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Code clarity, and improved opportunities for error detection. A `default` label
should say "everything else is handled the same way", and nothing else.

```cpp
enum E { a, b, c, d };

void f1(E x)
{
    switch (x) {
    case a:
        do_something();
        break;
    case b:
        do_something_else();
        break;
    default:
        take_the_default_action();
        break;
    }
}
```

Here it is clear that there is a default action and that cases `a` and `b` are
special.

But what if there is no default action and you mean to handle only specific
cases? In that case, have an empty `default`, or else it is impossible to know
whether you meant to handle all cases:

```cpp
void f2(E x)
{
    switch (x) {
    case a:
        do_something();
        break;
    case b:
        do_something_else();
        break;
    default:
        // do nothing for the rest of the cases
        break;
    }
}
```

If you leave out the `default`, a maintainer or a compiler might reasonably
assume that you intended to handle all cases:

```cpp
void f2(E x)
{
    switch (x) {
    case a:
        do_something();
        break;
    case b:
    case c:
        do_something_else();
        break;
    }
}
```

Did you forget case `d`, or deliberately leave it out? Forgetting a case
typically happens when an enumerator is added and the person adding it fails to
update every switch over the enumeration.

## Decision

Make each switch say explicitly what it does with the values no case names.

- Where there is a genuine common action for the remaining values, write it
  under `default`.
- Where the remaining values are deliberately ignored, write an empty `default`
  with a comment saying so, rather than omitting the label.
- Where every enumerator is meant to be handled, omit `default` entirely and let
  the compiler warn when an enumerator is added; do not write a `default` that
  merely does nothing in that case, because it disables the warning.
- Do not use `default` as a shorthand for a small set of values you could have
  named; list them.

## Rationale

An omitted `default` is ambiguous in a way that costs real bugs later: it means
either "all cases are covered" or "I did not think about the rest", and the two
readings diverge the moment an enumerator is added. Writing the empty `default`
resolves the ambiguity at the cost of three lines.

The tension in the rule is worth stating plainly. `default` is the tool that
documents intent for a reader, and it is also the tool that suppresses the
compiler's exhaustiveness warning. Which one wins depends on whether the switch
is meant to be exhaustive — that is the decision the author must actually make.

## Consequences

### Positive

- A reader can tell deliberate omission from oversight without consulting the
  author.
- Exhaustive switches keep the compiler's enumerator-coverage warning, which is
  the cheapest possible detection of an added enumerator.
- The special cases stand out, because `default` carries the bulk of the values.

### Negative

- The advice pulls in two directions: adding an empty `default` for clarity is
  exactly what stops the compiler telling you about a new enumerator, so the
  author must decide which switches are exhaustive and get it right.
- Empty `default` blocks add noise to short switches and can look like dead
  code to someone unfamiliar with the convention.
- Exhaustiveness warnings apply to enumerations only; a switch over integers or
  characters gets no such help, and there the rule is pure convention.

### Neutral

- Which values deserve a named case is a design question this rule does not
  answer.
- Whether the enumeration is a scoped `enum class`, and therefore whether the
  switch can be over unexpected values at all, is
  [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md).

## Enforcement

Flag switch-statements over an enumeration that do not handle all enumerators
and do not have a `default`. This might yield too many false positives in some
code bases; if so, flag only switches that handle most but not all cases, which
was the strategy of the very first C++ compiler.

## Related

- [0224](0224-es-78-dont-rely-on-implicit-fallthrough-in-switch-statements.md) —
  the other rule governing the same statement.
- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — the enumeration
  whose enumerators the switch is meant to cover.
- [0013](0013-p-7-catch-run-time-errors-early.md) — a missing case caught at
  compile time is the earliest possible catch.
- [0009](0009-p-3-express-intent.md) — the empty `default` states an intent the
  code otherwise leaves implicit.
