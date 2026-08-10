# 0073. C.52: Use inheriting constructors to import constructors into a derived class that does not need further explicit initialization

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If a derived class needs the base class's constructors, re-implementing them is
tedious and error-prone. `std::vector` has a lot of tricky constructors, and
someone writing their own vector on top of it does not want to reimplement them:

```cpp
class Rec {
    // ... data and lots of nice constructors ...
};

class Oper : public Rec {
    using Rec::Rec;
    // ... no data members ...
    // ... lots of nice utility functions ...
};
```

The condition on the rule is not decorative. A derived class that adds a member
gets constructors that do not know about it:

```cpp
struct Rec2 : public Rec {
    int x;
    using Rec::Rec;
};

Rec2 r {"foo", 7};
int val = r.x;   // uninitialized
```

## Decision

Import base constructors with `using Base::Base;` only when the derived class
adds nothing that needs initialising.

- Use an inheriting constructor when the derived class adds behaviour —
  utility functions, overrides — and no data.
- Do not use one when the derived class declares a data member, unless that
  member has an in-class initialiser that gives it a correct value for every
  imported constructor.
- If a derived member needs a value computed from the constructor's arguments,
  write the derived constructors out; the imported ones cannot see those
  arguments.
- Do not write out a base constructor by hand in the derived class when
  importing it would do, since the hand-written copy will drift from the base.

## Rationale

The rule is really two rules held together by one mechanism: importing
constructors is right when the derived class is a base with more functions, and
wrong when it is a base with more state. The `Rec2` example shows the second
case failing silently — the object is constructible, the member is garbage, and
no diagnostic appears anywhere.

An in-class initialiser on the added member converts the second case back into
the first, which is why
[0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
is the companion rule rather than an unrelated one.

## Consequences

### Positive

- The derived class tracks the base's constructor set automatically, including
  constructors added to the base later.
- Overload resolution, `explicit`, and `noexcept` are inherited exactly, rather
  than being approximated by hand.
- The derived class's definition shows what it adds, uncluttered by forwarding
  constructors.

### Negative

- The safety condition — no member needing initialisation — is not checked by
  the language, and it is violated by an edit to the derived class that has
  nothing to do with the `using` declaration.
- Inherited constructors are not considered for default construction, copy, or
  move, so the set that is imported is subtler than "all of them".
- A base constructor added later is imported silently, which can widen the
  derived class's interface without anyone deciding to.

### Neutral

- The rule is about constructors only; other base members are exposed by
  ordinary inheritance rules.
- Whether the inheritance itself is appropriate is a separate question this
  record does not address.

## Enforcement

- Make sure that every member of the derived class is initialised.

## Related

- [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
  — an in-class initialiser on the added member is what makes importing safe.
- [0072](0072-c-51-use-delegating-constructors-to-represent-common-actions-for-all-constructors-of-a-class.md)
  — the same objection to duplicating constructor work, within one class.
- [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) — a
  derived class in a hierarchy has other operations to think about besides
  construction.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — an interface base has no constructors worth importing.
