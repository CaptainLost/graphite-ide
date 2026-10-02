# 0325. T.20: Avoid "concepts" without meaningful semantics

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Concepts are meant to express semantic notions such as "a number", "a range" of
elements, and "totally ordered". Simple constraints such as "has a `+` operator"
and "has a `>` operator" cannot be meaningfully specified in isolation, and
should be used only as building blocks for meaningful concepts, not in user code.

```cpp
template<typename T>
// bad; insufficient
concept Addable = requires(T a, T b) { a+b; };

template<Addable N>
auto algo(const N& a, const N& b) // use two numbers
{
    // ...
    return a + b;
}

int x = 7;
int y = 9;
auto z = algo(x, y);   // z = 16

string xx = "7";
string yy = "9";
auto zz = algo(xx, yy);   // zz = "79"
```

Maybe the concatenation was expected. More likely it was an accident. Defining a
`Subtractable` equivalently would give a dramatically different set of accepted
types, and this `Addable` violates the mathematical rule that addition is
commutative: `a+b == b+a`.

The ability to specify meaningful semantics is the defining characteristic of a
true concept, as opposed to a syntactic constraint.

```cpp
template<typename T>
// The operators +, -, *, and / for a number are assumed to follow the usual mathematical rules
concept Number = requires(T a, T b) { a+b; a-b; a*b; a/b; };

template<Number N>
auto algo(const N& a, const N& b)
{
    // ...
    return a + b;
}

int x = 7;
int y = 9;
auto z = algo(x, y);   // z = 16

string xx = "7";
string yy = "9";
auto zz = algo(xx, yy);   // error: string is not a Number
```

Concepts with multiple operations have a far lower chance of accidentally
matching a type than single-operation concepts do.

## Decision

Define a concept only when it names something whose meaning can be stated.

- Name the abstraction — `Number`, `Range`, `Balancer` — not the operator the
  body happens to call.
- Require the coherent set of operations that abstraction implies, so that types
  which merely happen to spell one operator the same way are excluded.
- Keep single-operation constraints internal: use them to build larger concepts,
  and do not constrain a user-facing template with one directly.
- Write down the semantics the concept assumes, even as a comment, per
  [0327](0327-t-22-specify-axioms-for-concepts.md); if that cannot be done, the
  concept is not one.

## Rationale

A syntactic constraint tests for a spelling, not a meaning, and C++ deliberately
reuses spellings — `+` concatenates strings and adds integers — so a
single-operation constraint accepts every type that reused the token for an
unrelated purpose. The failure is silent: `algo(xx, yy)` on two strings returns
`"79"`, compiling and running and producing a plausible-looking answer, which is
worse than any compile error the constraint might have caused.

## Consequences

### Positive

- Types that merely share an operator spelling are rejected at the declaration
  rather than silently accepted.
- The set of concepts stays small and reusable, since each one names a real
  notion rather than one algorithm's needs.
- Requiring several operations at once makes accidental matches unlikely without
  any extra machinery.

### Negative

- It rules out a legitimate technique: single-operation detection traits are
  genuinely useful for library plumbing, and the rule permits them only as
  building blocks, which is a line no compiler can police.
- Requiring a coherent operation set makes concepts stricter than any one
  algorithm needs, colliding with
  [0333](0333-t-41-require-only-essential-properties-in-a-templates-concepts.md).
- "Meaningful semantics" is not checkable; the enforcement bullets can only find
  the crudest violations.
- Adding operations to make a concept meaningful may exclude types that would
  have worked, and the caller's remedy is to implement operators they had no use
  for.

### Neutral

- The rule is about concept definition, not about whether to constrain at all;
  that is [0321](0321-t-10-specify-concepts-for-all-template-arguments.md).
- The `Number` example's semantics live in a comment, which is all C++20 offers.

## Enforcement

- Flag single-operation concepts when used outside the definition of other
  concepts.
- Flag uses of `enable_if` that appear to simulate single-operation concepts.

## Related

- [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md) — the
  specific variant of this rule about operator sets.
- [0327](0327-t-22-specify-axioms-for-concepts.md) — how the semantics are
  written down.
- [0331](0331-t-26-prefer-to-define-concepts-in-terms-of-use-patterns-rather-than-simple-syntax.md)
  — the same objection to `has_equal`-style helper concepts.
- [0322](0322-t-11-whenever-possible-use-standard-concepts.md) — the standard
  concepts already have specified semantics.
