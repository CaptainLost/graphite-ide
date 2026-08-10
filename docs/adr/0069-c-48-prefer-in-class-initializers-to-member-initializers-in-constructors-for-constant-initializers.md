# 0069. C.48: Prefer in-class initializers to member initializers in constructors for constant initializers

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Putting a constant default on the member declaration makes it explicit that the
same value is expected in all constructors. It avoids repetition, avoids
maintenance problems, and leads to the shortest and most efficient code.

Spreading the constants across constructors leaves holes:

```cpp
class X {   // BAD
    int i;
    string s;
    int j;
public:
    X() :i{666}, s{"qqq"} { }   // j is uninitialized
    X(int ii) :i{ii} {}         // s is "" and j is uninitialized
    // ...
};
```

How would a maintainer know whether `j` was deliberately uninitialised —
probably a bad idea anyway — and whether it was intentional to give `s` the
default `""` in one case and `"qqq"` in another, which is almost certainly a
bug? Forgetting to initialise a member like `j` often happens when a new member
is added to an existing class.

With the defaults on the declarations, every constructor inherits them:

```cpp
class X2 {
    int i {666};
    string s {"qqq"};
    int j {0};
public:
    X2() = default;        // all members are initialized to their defaults
    X2(int ii) :i{ii} {}   // s and j initialized to their defaults
    // ...
};
```

Part of the benefit is available from default arguments to constructors, and
that is not uncommon in older code. However, it is less explicit, causes more
arguments to be passed, and is repetitive when there is more than one
constructor:

```cpp
class X3 {   // BAD: inexplicit, argument passing overhead
    int i;
    string s;
    int j;
public:
    X3(int ii = 666, const string& ss = "qqq", int jj = 0)
        :i{ii}, s{ss}, j{jj} { }   // all members are initialized to their defaults
    // ...
};
```

## Decision

Write a member's constant default once, on the member declaration.

- Give every member whose default does not depend on a constructor argument an
  in-class initialiser, using brace initialisation.
- Let each constructor mention only the members whose values it actually
  computes; the rest come from the declarations.
- Do not encode member defaults as default arguments on a constructor.
- When adding a member to an existing class, give it an in-class initialiser
  rather than editing every constructor's initialiser list.

## Rationale

The failure this rule prevents is a failure of omission, and omissions are
invisible in a diff. A new member added to `X` above compiles cleanly and is
uninitialised in both constructors; the same member added to `X2` cannot be
added without a value, because the place you add it is the place the value goes.

The `s` case is the second argument: two constructors disagreeing about a
default is indistinguishable, in the source, from two constructors deliberately
choosing different values. Writing the default once removes the possibility of
the question.

## Consequences

### Positive

- Every constructor, including ones written later, initialises every member.
- The default value is stated once, so two constructors cannot drift apart.
- Constructors shrink to the part that is actually specific to them.

### Negative

- The default is now in the header, so changing it recompiles every translation
  unit that includes the class, and it is visible to clients who do not need it.
- The initialiser has to be a valid expression at the point of the class
  definition, so the member's type must be complete there and every name the
  initialiser uses must already be visible, which pulls includes into the header
  that the declaration alone would not have needed.
- The rule applies only to constant initialisers, so a class with a mix of
  constant and computed defaults still has its initialisation described in two
  places.

### Neutral

- Default arguments on a constructor remain legal and are sometimes the right
  tool for an argument that is genuinely optional, as opposed to a member that
  has a default.
- `= default` on the default constructor is the natural companion to this rule
  and is governed by
  [0082](0082-c-80-use-default-if-you-have-to-be-explicit-about-using-the-default-semantics.md).

## Enforcement

- (Simple) Every constructor should initialise every member variable, either
  explicitly, via a delegating constructor call, or via default construction.
- (Simple) Default arguments to constructors suggest an in-class initialiser
  might be more appropriate.

## Related

- [0066](0066-c-45-dont-define-a-default-constructor-that-only-initializes-data-members-use-in-class-member-initializers-instead.md)
  — the same preference for a class whose only constructor is the default one.
- [0068](0068-c-47-define-and-initialize-member-variables-in-the-order-of-member-declaration.md)
  — fewer entries in the initialiser list means less order to keep straight.
- [0070](0070-c-49-prefer-initialization-to-assignment-in-constructors.md) — an
  in-class initialiser is initialisation, not assignment.
- [0013](0013-p-7-catch-run-time-errors-early.md) — an uninitialised member is
  the error this rule catches before it is ever read.
