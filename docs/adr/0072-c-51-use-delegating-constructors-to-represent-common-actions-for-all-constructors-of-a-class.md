# 0072. C.51: Use delegating constructors to represent common actions for all constructors of a class

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Repeating the same construction work in every constructor is tedious to write
and might accidentally not be common:

```cpp
class Date {   // BAD: repetitive
    int d;
    Month m;
    int y;
public:
    Date(int dd, Month mm, year yy)
        :d{dd}, m{mm}, y{yy}
        { if (!valid(d, m, y)) throw Bad_date{}; }

    Date(int dd, Month mm)
        :d{dd}, m{mm} y{current_year()}
        { if (!valid(d, m, y)) throw Bad_date{}; }
    // ...
};
```

Delegating to the fullest constructor states the relationship once:

```cpp
class Date2 {
    int d;
    Month m;
    int y;
public:
    Date2(int dd, Month mm, year yy)
        :d{dd}, m{mm}, y{yy}
        { if (!valid(d, m, y)) throw Bad_date{}; }

    Date2(int dd, Month mm)
        :Date2{dd, mm, current_year()} {}
    // ...
};
```

If the repeated action is a simple initialisation rather than a check, an
in-class member initialiser is the better tool.

## Decision

Express a constructor that is a special case of another by delegating to it.

- Nominate the constructor that takes the full set of parameters as the one that
  establishes the invariant, and have every other constructor delegate to it,
  supplying the defaults it fills in.
- Write the delegation as the sole entry in the initialiser list; the language
  forbids mixing a delegating call with member initialisers.
- Do not copy a validity check, a registration, or any other common action into
  more than one constructor.
- Where the common part is only a constant member value, use an in-class
  initialiser instead of delegation, per
  [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md).

## Rationale

The danger in the `Date` example is not the duplicated line, it is that the
duplicate can be edited alone. A check that appears in two constructors will one
day appear in a strengthened form in one of them, and nothing will report that
the other constructor now admits objects the class considers invalid.

Delegation makes the class have a single point at which the invariant is
established, which is the property that lets you reason about the class at all.

## Consequences

### Positive

- The invariant is established in exactly one place, so it cannot be
  strengthened in one constructor only.
- Adding a constructor becomes cheap: supply the defaults and delegate.
- The special-case constructors shrink to the arguments they actually decide.

### Negative

- A delegating constructor cannot initialise any member itself, so a constructor
  that would otherwise set one member differently must either be rewritten
  around the delegate's parameters or not delegate at all.
- The object is fully constructed by the delegate before the delegating
  constructor's body runs, so the destructor will run if that body fails — a
  different lifetime rule that is easy to overlook.
- Delegation chains obscure the order of work: reading one constructor no longer
  tells you what happens, and a delegation cycle is ill-formed with no
  diagnostic required, so compilers are not obliged to catch it.

### Neutral

- The example's common action is a throwing validity check, but the rule is
  about where a common action lives rather than about what it does; a
  registration, a logging call or a computed member all delegate the same way.
- Delegation replaces a private `init()` helper, which had the worse property of
  being callable after construction.

## Enforcement

- (Moderate) Look for similar constructor bodies.

## Related

- [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
  — the alternative when the common action is only a constant default.
- [0073](0073-c-52-use-inheriting-constructors-to-import-constructors-into-a-derived-class-that-does-not-need-further-explicit-initialization.md)
  — the same argument against re-implementing constructors, across an
  inheritance boundary.
- [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md)
  — where the common action cannot live in a constructor at all.
