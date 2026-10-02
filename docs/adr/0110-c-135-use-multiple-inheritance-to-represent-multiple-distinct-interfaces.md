# 0110. C.135: Use multiple inheritance to represent multiple distinct interfaces

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Not all classes will necessarily support all interfaces, and not all callers
will necessarily want to deal with all operations. Multiple inheritance is
especially useful for breaking apart monolithic interfaces into "aspects" of
behaviour supported by a given derived class.

```cpp
class iostream : public istream, public ostream {   // very simplified
    // ...
};
```

`istream` provides the interface to input operations; `ostream` provides the
interface to output operations. `iostream` provides the union of the `istream`
and `ostream` interfaces, and the synchronisation needed to allow both on a
single stream.

This is a very common use of inheritance, because the need for multiple
different interfaces to an implementation is common and such interfaces are
often not easily or naturally organised into a single-rooted hierarchy. Such
interfaces are typically abstract classes.

The alternative that this rule rejects is the single fat interface: one base
class carrying every operation any implementation might support, which is what
[0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md)
warns against. Splitting it into aspects means a class that can only be read
from derives from the reading interface alone, and a function that only reads
takes a parameter of that interface — so the type system records which
operations are actually in play.

## Decision

Where a class supports several independent sets of operations, express each as
its own abstract base and inherit from all of them.

- Split an interface along the lines of what implementations actually support,
  not along what is convenient to declare in one place.
- Make each such base a pure abstract class with a virtual destructor and no
  data, per
  [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md).
- Have a function take the narrowest interface it actually uses, so that a
  read-only operation cannot be handed something it might write.
- Where a class needs to be reachable through two interfaces that themselves
  share a base, make that shared base virtual, per
  [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md).
- Do not put state in these bases; multiple inheritance of state is a different
  and heavier proposition, covered by
  [0111](0111-c-136-use-multiple-inheritance-to-represent-the-union-of-implementation-attributes.md).

## Rationale

Multiple inheritance of pure interfaces is the cheap case: with no data in any
base, there is no layout conflict, no ambiguity about which copy of a member is
meant, and no diamond problem to solve. The reputation multiple inheritance has
is earned by inheriting state, not by this.

The design benefit is that the aspects are independently satisfiable. A type
that can be drawn but not serialised simply does not derive from the
serialisation interface, and every attempt to serialise it fails to compile —
which is what the single fat interface cannot express, since there it would have
to be a function that throws.

## Consequences

### Positive

- A function can require exactly the operations it uses, and no more.
- Implementations declare which aspects they support, and unsupported
  operations are absent rather than throwing.
- Interfaces that have no natural common root do not have to be forced into one.

### Negative

- The number of interface types grows, and callers who legitimately need two
  aspects at once must either take two references to the same object or declare
  a combined interface.
- Once two of these interfaces share a base, virtual inheritance becomes
  necessary, and it brings a heavier object layout and its own initialisation
  rules.
- Fine-grained interfaces make it harder to see the whole of what a type can do;
  the capability is spread across several declarations.

### Neutral

- The rule concerns interfaces only; state is
  [0111](0111-c-136-use-multiple-inheritance-to-represent-the-union-of-implementation-attributes.md)'s
  subject.
- Whether the combined interface — `iostream` — should also exist is a separate
  convenience question.

## Related

- [0111](0111-c-136-use-multiple-inheritance-to-represent-the-union-of-implementation-attributes.md)
  — the same mechanism applied to implementation rather than interface.
- [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md) —
  what to do when the aspects share a base.
- [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md)
  — the over-general single interface this rule breaks up.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the empty abstract classes being inherited here.
