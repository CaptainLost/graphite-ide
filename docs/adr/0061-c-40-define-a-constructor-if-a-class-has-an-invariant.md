# 0061. C.40: Define a constructor if a class has an invariant

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

That is what constructors are for. A constructor is the one place the language
guarantees will run before any member function can be called, which makes it the
only place an invariant can be established.

```cpp
class Date {  // a Date represents a valid date
              // in the January 1, 1900 to December 31, 2100 range
    Date(int dd, int mm, int yy)
        :d{dd}, m{mm}, y{yy}
    {
        if (!is_valid(d, m, y)) throw Bad_date{};  // enforce invariant
    }
    // ...
private:
    int d, m, y;
};
```

It is often a good idea to express the invariant as an `Ensures` on the
constructor.

A constructor can be used for convenience even where a class has no invariant:

```cpp
struct Rec {
    string s;
    int i {0};
    Rec(const string& ss) : s{ss} {}
    Rec(int ii) :i{ii} {}
};

Rec r1 {7};
Rec r2 {"Foo bar"};
```

The C++11 initializer list rule eliminates the need for many constructors:

```cpp
struct Rec2{
    string s;
    int i;
    Rec2(const string& ss, int ii = 0) :s{ss}, i{ii} {}   // redundant
};

Rec2 r1 {"Foo", 7};
Rec2 r2 {"Bar"};
```

The `Rec2` constructor is redundant, and the default for the `int` would be
better expressed as a member initialiser.

## Decision

Where a class has an invariant, give it a constructor that establishes it, and
give it no way to be constructed without one.

- Identify the condition over the members that every public operation is allowed
  to assume, and establish it in every constructor.
- State the condition where the reader will find it: as a comment on the class,
  or as an `Ensures` on the constructor, per
  [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md).
- Make the constrained members private so that no other route to an
  unconstrained object exists, per
  [0047](0047-c-9-minimize-exposure-of-members.md).
- Reject invalid arguments rather than storing them; how the rejection is
  reported, and when a factory function is the better shape, is
  [0063](0063-c-42-if-a-constructor-cannot-construct-a-valid-object-throw-an-exception.md).
- Do not write a constructor that only repeats what member initialisers and
  aggregate initialisation already do. A class with no invariant needs no
  constructor.

## Rationale

An invariant that is not established in a constructor is not an invariant, it is
a hope: every member function must then either check the condition itself or
assume something the type does not guarantee. The constructor is what converts a
documented expectation into a property of every object that exists.

The redundant-constructor note is the necessary counterweight. Constructors are
not free — one user-declared constructor removes the implicitly generated
default constructor and disables aggregate initialisation — so writing them for
types that have nothing to enforce costs the type its simplest uses in exchange
for nothing.

## Consequences

### Positive

- Every object of the type satisfies its invariant from the moment it exists.
- Member functions can assume the condition instead of re-checking it.
- The invariant is written down in one place rather than implied by scattered
  checks.

### Negative

- Declaring a constructor suppresses the implicit default constructor and
  aggregate initialisation, so adding one to an existing type breaks every
  brace-initialisation and designated initialiser at its call sites.
- A constructor that must reject its arguments has no return value to report the
  rejection through, which is precisely the gap
  [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  records; the workaround is a private constructor and a static factory, which
  is more machinery than a constructor.
- Validation in the constructor is paid on every construction, including the
  ones the caller knows are valid, and there is no standard way to opt out.

### Neutral

- A constructor may be provided for convenience where there is no invariant;
  this record does not forbid that, it only declines to require it.
- The rule says the invariant must be established, not how the members are
  stored.

## Enforcement

- Flag classes with user-defined copy operations but no constructor. A
  user-defined copy operation is a good indicator that the class has an
  invariant.

## Related

- [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md)
  — where the invariant is first identified.
- [0062](0062-c-41-a-constructor-should-create-a-fully-initialized-object.md) —
  what the constructor must achieve.
- [0063](0063-c-42-if-a-constructor-cannot-construct-a-valid-object-throw-an-exception.md)
  — what to do when it cannot.
- [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) — the
  invariant stated as a constructor postcondition.
