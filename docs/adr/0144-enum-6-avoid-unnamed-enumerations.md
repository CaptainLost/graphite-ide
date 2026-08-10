# 0144. Enum.6: Avoid unnamed enumerations

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

If you cannot name an enumeration, the values are not related.

```cpp
enum { red = 0xFF0000, scale = 4, is_signed = 1 };
```

A colour, a scale factor and a flag have been put in one declaration, and the
only thing they have in common is that each is an integer known at compile time.
No name exists for that set because there is nothing to name. Such code is not
uncommon in code written before there were convenient alternative ways of
specifying integer constants: the unnamed enumeration was the way to get a
compile-time integer constant with a name attached, and it was used for that
regardless of whether the constants formed a set.

The alternative is to use `constexpr` values instead. For example:

```cpp
constexpr int red = 0xFF0000;
constexpr short scale = 4;
constexpr bool is_signed = true;
```

Which is better in a second way as well. The unnamed enumeration forced all
three constants into one underlying integer type; declared separately, each gets
the type it should have had, and `is_signed` becomes a `bool` rather than a `1`.

## Decision

Do not declare unnamed enumerations.

- Where the constants form a set, name the enumeration after the set, and prefer
  `enum class`.
- Where they do not form a set, declare each as its own `constexpr` variable,
  with the type that fits it — `bool` for a flag, `short` for a small count, not
  `int` for everything.
- Do not use an unnamed enumeration to obtain a compile-time integer constant
  inside a class; a `static constexpr` data member does that, and since C++17 it
  is implicitly inline and needs no out-of-line definition, so the enum hack has
  no remaining advantage.
- If naming the enumeration is hard, treat that as evidence that the grouping is
  wrong rather than as a naming problem to push through.

## Rationale

The rule's real content is the diagnostic in its reason, not the prohibition.
Being unable to name the enumeration is information: it says the declaration
groups things that a reader will now assume belong together, and they do not.
Renaming does not fix that, and splitting does.

The historical reading matters for how the rule is applied. An unnamed
enumeration in old code is usually not a claim that the constants are related;
it is the enum hack, reaching for the only tool that then existed. `constexpr`
supersedes it outright, which is why this rule can be stated as a flat
prohibition rather than a preference.

## Consequences

### Positive

- Each constant gets its own type instead of sharing one arbitrary integer type
  with unrelated constants.
- A declaration that groups constants now means they are a group, so the
  grouping carries information.
- The named enumerations that survive can be used as parameter and return types,
  which an unnamed one cannot.

### Negative

- Namespace-scope `constexpr` variables have internal linkage, so a constant
  declared in a header is a separate object in every translation unit that
  includes it. That is invisible until something takes its address or its size
  matters; `inline constexpr` is the fix, and it has to be remembered.
- Splitting one declaration into several is more lines for the same constants,
  and rejects the compact form for genuinely incidental values.
- The rule offers no help with the case it is hardest on: constants that are
  weakly related, where naming the group is possible but the name is a
  fabrication, and both the enumeration and the loose constants are
  unsatisfying.
- Enumerators and `constexpr` variables are not interchangeable at every use
  site; code relying on an enumerator's behaviour in overload resolution or in
  template arguments may need adjusting when it becomes a variable.

### Neutral

- The rule does not say every group of constants deserves an enumeration; it
  says an enumeration that cannot be named is not a group.
- Whether the resulting named enumeration is scoped is settled by
  [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md), not here.

## Enforcement

Flag unnamed enumerations.

## Related

- [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md)
  — the positive form: enumerate the constants that really are a set.
- [0139](0139-enum-1-prefer-enumerations-over-macros.md) — the other rule that
  sends unrelated constants to `constexpr`.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — giving each
  constant its own type is the same instinct at a smaller scale.
- [0009](0009-p-3-express-intent.md) — a declaration that cannot be named does
  not express one.
