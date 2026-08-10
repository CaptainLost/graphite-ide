# 0101. C.126: An abstract class typically doesn't need a user-written constructor

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

An abstract class typically does not have any data for a constructor to
initialise.

```cpp
class Shape {
public:
    // no user-written constructor needed in abstract base class
    virtual Point center() const = 0;    // pure virtual
    virtual void move(Point to) = 0;
    // ... more pure virtual functions...
    virtual ~Shape() {}                 // destructor
};

class Circle : public Shape {
public:
    Circle(Point p, int rad);           // constructor in derived class
    Point center() const override { return x; }
};
```

The rule follows from
[0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md):
if the interface has no data, a constructor has nothing to do, and writing one
adds a step to every derived class's initialisation without adding an effect. It
is also a useful signal in reverse — an abstract class that seems to need a
constructor usually has state it should not have.

There are two exceptions. A base class constructor that does work — such as
registering the object somewhere — might legitimately need one. And in extremely
rare cases it may be reasonable for an abstract class to hold a little data
shared by all derived classes, such as usage statistics or debug information;
such classes tend to have constructors. But be warned: such classes also tend to
be prone to requiring virtual inheritance.

## Decision

Do not write a constructor for an abstract class unless it has work to do.

- Give the abstract class a virtual destructor and no constructor; the implicit
  default constructor is what derived classes need.
- Put initialisation of a derived class's state in that derived class's
  constructor.
- Treat the appearance of a constructor on an abstract class as a prompt to ask
  whether the state it initialises belongs in the interface at all.
- Where an abstract base genuinely performs an action on construction, such as
  registration, write the constructor and say in a comment what work it does.
- Where an abstract base genuinely holds shared incidental data, expect to need
  virtual inheritance if the hierarchy later acquires multiple paths to that
  base, and prefer moving the data out.

## Rationale

A constructor on an abstract class is not merely redundant; it is a claim that
the interface has state, and that claim propagates. Every derived class must
name the base in its member initialiser list, and every change to the base's
data touches all of them.

The registration exception is worth keeping in view because it names the one
case where the base is doing something rather than holding something. Even
there, doing work in a constructor on a not-yet-fully-constructed object is
delicate — virtual dispatch during base construction resolves to the base — so
the exception is narrow.

## Consequences

### Positive

- Derived classes initialise only their own state, so their constructors are
  shorter.
- The absence of a constructor is a visible confirmation that the interface
  holds no data.
- Adding a derived class requires no coordination with the base's construction
  protocol.

### Negative

- Where an invariant genuinely does span the whole hierarchy, this rule pushes
  its establishment into every derived class, where it can be forgotten. There
  is no single place to enforce it.
- The registration exception has a hazard the rule does not resolve: a virtual
  call from a base constructor dispatches to the base, so a registering
  constructor cannot consult the derived object.
- The "rare shared data" exception is stated but discouraged, leaving the author
  of such a class with a warning and no recommended alternative beyond
  avoidance.

### Neutral

- The destructor is a separate matter and is required; see
  [0102](0102-c-127-a-class-with-a-virtual-function-should-have-a-virtual-or-protected-destructor.md).
- Protected copy and move operations on an abstract base are also not covered by
  this rule, though a polymorphic base commonly needs them; see
  [0105](0105-c-130-for-making-deep-copies-of-polymorphic-classes-prefer-a-virtual-clone-function-instead-of-public-copy-construction-assignment.md).

## Enforcement

- Flag abstract classes with constructors.

## Related

- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the no-data rule this one follows from.
- [0102](0102-c-127-a-class-with-a-virtual-function-should-have-a-virtual-or-protected-destructor.md)
  — the one special member an abstract class does need.
- [0108](0108-c-133-avoid-protected-data.md) — the state an abstract-class
  constructor would be initialising is usually protected data.
- [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md) —
  the virtual inheritance the exception warns about.
