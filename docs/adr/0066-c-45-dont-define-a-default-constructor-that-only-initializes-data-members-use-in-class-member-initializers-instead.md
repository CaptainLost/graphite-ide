# 0066. C.45: Don't define a default constructor that only initializes data members; use in-class member initializers instead

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Using in-class member initialisers lets the compiler generate the function for
you, and the compiler-generated function can be more efficient.

A hand-written default constructor whose body and initialiser list do nothing
the compiler would not have done is pure overhead in source terms:

```cpp
class X1 { // BAD: doesn't use member initializers
    string s;
    int i;
public:
    X1() :s{"default"}, i{1} { }
    // ...
};
```

Moving the values to the declarations removes the constructor entirely:

```cpp
class X2 {
    string s = "default";
    int i = 1;
public:
    // use compiler-generated default constructor
    // ...
};
```

## Decision

Do not write a default constructor whose only effect is to give members
constant values.

- Put the value on the member declaration and let the implicit default
  constructor do the work.
- If a default constructor must exist explicitly — because another constructor
  suppresses the implicit one, or because the declaration order matters for
  documentation — write `X() = default;` rather than a body.
- Keep a written-out default constructor only when it does something beyond
  member initialisation: establishing an invariant that spans members,
  registering the object, acquiring a resource, or computing a value from
  another member.

## Rationale

The compiler-generated default constructor is not merely shorter; it is the one
the language's trait machinery can reason about. Writing the body by hand can
cost the type its trivial or `constexpr` status and thereby change how it
behaves in containers and in constant expressions, for no gain.

The maintenance argument is the sharper one: a value written at the member's
declaration is seen by every constructor and by every future constructor, while
a value written in one constructor's initialiser list is seen by that
constructor alone.

## Consequences

### Positive

- Adding a constructor later cannot forget a default, because the default lives
  on the member.
- The type can stay trivially default-constructible or `constexpr`-constructible
  where it otherwise would not.
- The class shrinks: the default values are read next to the members they
  belong to.

### Negative

- An in-class initialiser is part of the class definition, so in a header it is
  visible to every translation unit and changing it recompiles all of them; a
  value in a constructor body in a `.cpp` does not.
- Non-constant or expensive defaults do not move cleanly to the declaration, so
  the rule applies unevenly across a class and can leave initialisation split
  between two places.
- In-class initialisers make a class non-aggregate in older standards, which can
  break brace initialisation for code that relied on it; C++14 onwards relaxes
  this, so the hazard is confined to code that must still compile as C++11.

### Neutral

- The rule is about where the constant lives, not about which constants are
  right.
- `= default` is the explicit form when the constructor must be declared, and is
  governed by [0082](0082-c-80-use-default-if-you-have-to-be-explicit-about-using-the-default-semantics.md).

## Enforcement

- (Simple) A default constructor should do more than just initialise member
  variables with constants.

## Related

- [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
  — the same preference, stated for classes that have other constructors too.
- [0082](0082-c-80-use-default-if-you-have-to-be-explicit-about-using-the-default-semantics.md)
  — how to declare the constructor when it must appear.
- [0065](0065-c-44-prefer-default-constructors-to-be-simple-and-non-throwing.md)
  — a generated default constructor is simple and non-throwing by construction.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — "this member defaults to
  1" is said better by the declaration than by a constructor.
