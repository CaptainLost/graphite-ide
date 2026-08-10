# 0068. C.47: Define and initialize member variables in the order of member declaration

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Members are initialised in declaration order, independent of the order of the
member initialisers. Writing the initialiser list in a different order therefore
describes something that will not happen:

```cpp
class Foo {
    int m1;
    int m2;
public:
    Foo(int x) :m2{x}, m1{++x} { }   // BAD: misleading initializer order
    // ...
};
Foo x(1); // surprise: x.m1 == x.m2 == 2
```

`m1` is initialised first, from `++x`, which makes it `2` and leaves `x` at `2`;
`m2` is initialised second, from the now-incremented `x`, and is also `2`. The
reader who trusts the written order predicts `m2 == 1`.

## Decision

Write member initialisers in the same order as the member declarations.

- List every initialiser in the initialiser list in declaration order, base
  classes first.
- When an initialiser reads another member, satisfy yourself that the member it
  reads is declared earlier; if it is not, reorder the declarations or compute
  the value from the constructor's arguments instead.
- Do not write an initialiser whose value depends on a side effect performed by
  another initialiser in the same list.
- Enable the compiler's reorder warning and treat it as an error, which makes
  the rule mechanical rather than a matter of discipline.

## Rationale

The language fixed the initialisation order so that the destructor can run in
exactly the reverse order regardless of what any constructor wrote. That order
cannot be changed, so the only thing a mismatched initialiser list can do is
mislead the reader.

The bug this prevents is not a compile error and not a crash: it is a value
being silently one different from the one the source appears to specify, which
is the hardest kind to find.

## Consequences

### Positive

- The written order and the executed order agree, so the constructor can be read
  top to bottom.
- Dependencies between members become visible as an ordering constraint on the
  declarations rather than a hidden assumption.
- The compiler's reorder diagnostic becomes usable instead of being routinely
  suppressed.

### Negative

- Member declaration order is also constrained by layout, padding, and
  readability, and this rule adds a fourth constraint that can conflict with
  them; satisfying it may mean a worse-packed struct.
- Reordering members to satisfy an initialiser is an ABI change, so the rule can
  be expensive to apply to an existing published type.
- It is a convention about appearance, not about behaviour: obeying it does not
  by itself stop a member initialiser from reading an uninitialised member, it
  only makes the mistake easier to see.

### Neutral

- This rule constrains the order of initialisers, not which members have them;
  what to do about members with constant defaults is
  [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md).
- Base-class subobjects follow the same rule and are always initialised before
  any member.

## Enforcement

- (Simple) A member initialiser list should mention the members in the same
  order they are declared.

## Related

- [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
  — moving constant initialisers out of the list shortens what has to be
  ordered.
- [0070](0070-c-49-prefer-initialization-to-assignment-in-constructors.md) — both
  records are about not using a member before it is set.
- [0072](0072-c-51-use-delegating-constructors-to-represent-common-actions-for-all-constructors-of-a-class.md)
  — delegation gives one initialiser list to keep in order instead of several.
- [0009](0009-p-3-express-intent.md) — an initialiser list in a different order
  states an intent the language will not honour.
