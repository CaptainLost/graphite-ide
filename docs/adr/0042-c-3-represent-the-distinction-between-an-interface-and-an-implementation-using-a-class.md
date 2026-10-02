# 0042. C.3: Represent the distinction between an interface and an implementation using a class

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

An explicit distinction between interface and implementation improves
readability and simplifies maintenance. A class with a public section and a
private section draws that line in one place, in a form the compiler enforces.

```cpp
class Date {
public:
    Date();
    // validate that {yy, mm, dd} is a valid date and initialize
    Date(int yy, Month mm, char dd);
    int day() const;
    Month month() const;
    // ...
private:
    // ... some representation ...
};
```

With this shape, the representation of a `Date` can be changed without affecting
its users, although recompilation is likely. What the users depend on is the
public section; what changes is the private one.

Using a class this way is not the only way to draw the line. A set of
declarations of freestanding functions in a namespace, an abstract base class,
or a function template constrained by concepts can each represent an interface.
The important issue is that an interface and its implementation details are
explicitly distinguished at all. Ideally, and typically, an interface is far
more stable than its implementations.

## Decision

Make the boundary between what a type promises and how it delivers that promise
explicit in the code.

- Put the promise in the public section of a class and the representation in the
  private section, and let access control be what enforces the boundary.
- Where a class is not the right vehicle, use one of the alternatives — a
  namespace of free functions, an abstract base class, or a constrained function
  template — but pick one, deliberately, rather than leaving the boundary
  implicit.
- Treat the public section as the stable part. Changes to the private section
  should not require any user to change code.
- Do not expose the representation as a convenience and then rely on users not
  to touch it; there is then no boundary, only a hope.

## Rationale

The rule is what makes every other encapsulation rule pay off. If the boundary
is not stated, the set of things a change can break is the whole program, and
the cost of a change is unknowable in advance. Stating it reduces that set to
the declared interface, which is finite, reviewable, and small.

The guideline's own qualification is the part worth keeping in view: the class
is a mechanism, not the point. Recompilation still follows a private change,
because the private members are in the header; if that matters, the boundary has
to be drawn with a different tool, which is what
[0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) is for.

## Consequences

### Positive

- The representation can change without a user-visible change.
- The reviewable surface of a type is finite and stated in one place.
- Access control gives the boundary compiler backing rather than convention.

### Negative

- A class in a header publishes its private members to every translation unit
  that includes it, so the boundary is a compilation boundary in name only —
  changing a private member still forces rebuilds.
- The interface/implementation split adds accessors and forwarding for data that
  callers legitimately need, which is code that exists only to maintain the
  boundary.
- Deciding what belongs in the public section is a design act performed before
  the users exist, and an interface guessed too early is either too narrow to
  use or too wide to change.

### Neutral

- Which mechanism draws the line — class, namespace, abstract base, constrained
  template — is a separate choice made per case.
- The rule concerns declaration, not physical file layout.

## Related

- [0020](0020-i-1-make-interfaces-explicit.md) — an interface that is explicit
  in the first place is what this record makes structural.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the abstract-base-class form of the same distinction.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — what to
  do when the recompilation this record concedes is unacceptable.
- [0047](0047-c-9-minimize-exposure-of-members.md) — how far the public section
  should extend.
