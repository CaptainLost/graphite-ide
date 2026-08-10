# 0111. C.136: Use multiple inheritance to represent the union of implementation attributes

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Some forms of mixin have state, and often operations on that state. If those
operations are virtual, the use of inheritance is necessary; if they are not,
using inheritance can still avoid boilerplate and forwarding.

```cpp
class iostream : public istream, public ostream {   // very simplified
    // ...
};
```

`istream` provides the interface to input operations and some data; `ostream`
provides the interface to output operations and some data. `iostream` provides
the union of the `istream` and `ostream` interfaces and the synchronisation
needed to allow both on a single stream. This is the same example as
[0110](0110-c-135-use-multiple-inheritance-to-represent-multiple-distinct-interfaces.md)
uses, read from the implementation side rather than the interface side: the
bases contribute buffers and positions, not only signatures.

This is a relatively rare use, because implementation can often be organised
into a single-rooted hierarchy.

Sometimes an implementation attribute is more like a mixin that determines the
behaviour of an implementation and injects members to enable the implementation
of the policies it requires. `std::enable_shared_from_this` is one example, and
the intrusive bases from Boost — `list_base_hook`, `intrusive_ref_counter` — are
others. Each contributes a small piece of state and the operations over it, and
a class picks up the ones it needs.

Because these bases carry data, the concerns that
[0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
raises apply in full: the layout of the derived class depends on all of them,
and a change to any base's members affects everything below it.

## Decision

Use multiple inheritance to combine implementation attributes only when
composition will not do.

- Inherit an implementation attribute when it has virtual operations the derived
  class must participate in, or when the attribute must know the derived type,
  as `enable_shared_from_this` and intrusive hooks do.
- Otherwise hold the attribute as a member and forward, which keeps the layout
  and the coupling simpler.
- Keep each attribute base's data private to it, exposing operations rather than
  state, per [0108](0108-c-133-avoid-protected-data.md).
- Keep each attribute independent: two attribute bases should not need to know
  about each other, and any coordination between them belongs in the derived
  class.
- Where two attribute bases share a base of their own, make that base virtual,
  per
  [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md).

## Rationale

The rule earns its place by being narrow. Its first sentence is a test, not a
recommendation: inheritance is *necessary* when the operations are virtual, and
merely *convenient* — it saves forwarding — when they are not. Everywhere else,
holding the attribute as a member gives the same behaviour with less coupling
and a simpler object layout.

The examples worth remembering are the ones where the base must know its derived
type. `enable_shared_from_this<T>` cannot be a member, because its whole purpose
is to relate the base sub-object to the complete object; the same is true of an
intrusive list hook. Those are genuine inheritance, not reuse.

## Consequences

### Positive

- An attribute with virtual operations can be combined without hand-written
  forwarding.
- Mixins that must know the derived type are expressible at all.
- Each attribute stays a small, independently testable unit.

### Negative

- Inheriting state fixes the derived class's layout to the union of every base's
  members, so any base's change is an ABI change for everything below it.
- Two attribute bases that turn out to share a base introduce a diamond, and
  virtual inheritance to resolve it, with its layout and initialisation costs.
- Inheritance is public by default in the reader's mind: an implementation
  attribute inherited publicly leaks into the derived class's interface, and
  making it private then blocks the very conversions some mixins need.
- The rule is explicitly rare, so it is easy to over-apply — every "reuse by
  inheritance" mistake can be described as an implementation attribute.

### Neutral

- Composition remains the default; this rule names the exceptions.
- Nothing here concerns pure interfaces, which are
  [0110](0110-c-135-use-multiple-inheritance-to-represent-multiple-distinct-interfaces.md)'s
  subject and carry none of these costs.

## Related

- [0110](0110-c-135-use-multiple-inheritance-to-represent-multiple-distinct-interfaces.md)
  — the same mechanism used for interfaces, where it is cheap.
- [0112](0112-c-137-use-virtual-bases-to-avoid-overly-general-base-classes.md) —
  the resolution when attribute bases collide.
- [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
  — why data in a base is the expensive kind of inheritance.
- [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md)
  — the standing preference for a member over a base.
