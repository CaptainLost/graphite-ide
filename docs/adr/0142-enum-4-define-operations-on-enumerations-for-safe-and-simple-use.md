# 0142. Enum.4: Define operations on enumerations for safe and simple use

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Enum (Enumerations), transcribed 2026-08-10

## Context

The reasons are convenience of use and avoidance of errors. An enumeration
carries no operations of its own beyond comparison, so any use that goes past
naming a value — stepping through a sequence, combining flags, wrapping around —
has to be written somewhere. The choice is between writing it once, next to the
enumeration, and writing a cast at every call site that needs it.

```cpp
enum Day { mon, tue, wed, thu, fri, sat, sun };

Day& operator++(Day& d)
{
    return d = (d == Day::sun) ? Day::mon : static_cast<Day>(static_cast<int>(d)+1);
}

Day today = Day::sat;
Day tomorrow = ++today;
```

The use of a `static_cast` is not pretty, but

```cpp
Day& operator++(Day& d)
{
    return d = (d == Day::sun) ? Day::mon : Day{++d};    // error
}
```

is an infinite recursion, and writing it without a cast, using a switch on all
cases, is long-winded. So the cast is not avoidable; it is only placeable. Put
it inside the operator, where it is written once, reviewed once, and covered by
whatever tests the enumeration has — and where the wrap from `sun` back to `mon`
is handled in the same place rather than at each caller who remembered it.

## Decision

Give an enumeration the operations its users would otherwise improvise.

- Define the operations the domain actually has: `operator++` for a cyclic
  sequence, `|` and `&` for a set of flags, a conversion to text for
  diagnostics.
- Keep every `static_cast` between the enumeration and its underlying type
  inside those operations, so that no caller writes one.
- Put the domain's edge cases — the wrap-around, the empty flag set, the value
  with no name — in the operation, so they are decided once.
- Declare the operations in the same header and namespace as the enumeration, so
  that argument-dependent lookup finds them wherever the type is used.
- Do not define an operation the domain does not have. Incrementing a `Day` is
  meaningful; incrementing a `Product_info` is not, and an operator that exists
  will be used.

## Rationale

The casts are the point. A scoped enumeration deliberately removes the implicit
conversion to an integer, which means every non-trivial use has to reintroduce
it by hand — and a cast written at a call site is unchecked, unreviewed and
duplicated. Defining the operation converts a cast repeated everywhere into a
cast in one place with a name on it.

The wrap-around in the example makes the second half of the argument: the
operator is not merely sugar, it is where the domain rule lives. `sat + 1` is
`sun` and `sun + 1` is `mon`, and no caller doing its own arithmetic will
remember the second half.

## Consequences

### Positive

- Callers stop writing casts, so the unchecked conversions in the codebase
  reduce to a countable set.
- Domain rules such as wrap-around are stated once and cannot be got wrong per
  call site.
- Uses of the enumeration read as operations on a type rather than as arithmetic
  on a disguised integer.

### Negative

- The operator bodies contain exactly the casts the rule elsewhere warns
  against, and a cast inside `operator++` is no safer than one outside it — it
  is only in a better place. Casting a value outside the enumeration's range is
  undefined behaviour when the enumeration has no fixed underlying type, and
  when it has one the result is well defined but still names no enumerator.
- The operations must be maintained in step with the enumerators. Adding a day
  after `sun` silently breaks the wrap in the example, and nothing in the
  language connects the two.
- It is a genuine cost per enumeration: a flags type needs `|`, `&`, `^`, `~`
  and their assigning forms before it is usable, which is a lot of code for what
  a plain enumeration got for free.
- An operator makes the operation available everywhere the type is, including
  where it makes no sense; `++` on an enumeration that is a set of alternatives
  invites nonsense that compiles.

### Neutral

- Upstream's example uses a plain `enum`, which
  [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) advises against; as
  an `enum class` the same code needs the inner `static_cast<int>` for exactly
  the same reason, so the shape of the operator does not change.
- The rule does not say which operations an enumeration should have; that
  follows from the domain.

## Enforcement

Flag repeated expressions cast back into an enumeration.

## Related

- [0141](0141-enum-3-prefer-class-enums-over-plain-enums.md) — scoping is what
  creates the need for these operations.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — putting the cast in the operator is that rule applied to enumerations.
- [0140](0140-enum-2-use-enumerations-to-represent-sets-of-related-named-constants.md)
  — the enumeration whose operations these are.
- [0146](0146-enum-8-specify-enumerator-values-only-when-necessary.md) — flag
  sets are the case where the values must be chosen deliberately.
