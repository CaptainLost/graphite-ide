# 0107. C.132: Don't make a function virtual without reason

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A redundant `virtual` increases run time and object-code size. A virtual
function can be overridden and is thus open to mistakes in a derived class. A
virtual function also ensures code replication in a templated hierarchy, because
the function cannot be discarded even when it is never called.

```cpp
// bad
template<class T>
class Vector {
public:
    // ...
    virtual int size() const { return sz; }   // bad: what good could a derived class do?
private:
    T* elem;   // the elements
    int sz;    // number of elements
};
```

This kind of "vector" is not meant to be used as a base class at all. Making
`size()` virtual gives the class a vtable, makes it non-trivially destructible
in the usual case where a virtual destructor follows, prevents `size()` from
being inlined through a base reference, and invites a derived class to override
it with something that disagrees with `sz` — none of which buys anything, since
there is no plausible derived class that would want a different answer.

The question the rule asks is what a derived class could usefully do with the
function. If the answer is nothing, `virtual` is a cost with no corresponding
benefit. If the answer is something, the function is a customisation point and
should be virtual, and probably documented as such.

## Decision

Make a function virtual only when a derived class has a reason to override it.

- Before writing `virtual`, name the derived class that would override it and
  what it would do differently. If neither can be named, leave the function
  non-virtual.
- Do not make a whole class's interface virtual by reflex because one function
  needs to be.
- Do not make a function virtual for testability alone where a template
  parameter, a function object, or an injected interface would serve.
- A class not intended as a base should have no virtual functions at all, and
  therefore no virtual destructor.
- Treat a virtual function as a published customisation point: its signature and
  its contract are part of the interface to every derived class, present and
  future.

## Rationale

The costs are cumulative and mostly invisible at the point where `virtual` is
typed. The vtable pointer changes the object's size and layout, a virtual
destructor makes the type non-trivial, calls stop being inlined, and in a
template the definition can no longer be discarded. None of that shows up in the
line that caused it.

The correctness argument is the stronger one. A virtual function is an open
extension point: any derived class may replace it, including with an
implementation that violates the base's invariant. `size()` returning something
other than `sz` is a bug that the base class has invited and cannot prevent.

## Consequences

### Positive

- Non-polymorphic classes stay small, trivially copyable and inlinable.
- The set of functions a derived class may change stays small and deliberate.
- In templates, unused member functions can be discarded, so code size stays
  down.

### Negative

- A function that turns out to need overriding later cannot be made virtual
  without changing the class's layout and ABI, so this rule trades future
  flexibility for present cost.
- It sits against the common practice of making everything virtual for mocking,
  and offers alternatives — templates, injected interfaces — that are more work
  to set up.
- "Without reason" is unfalsifiable at the moment of writing; the reason
  frequently appears a year later, and the rule gives no guidance on how much
  anticipation is legitimate.

### Neutral

- The rule is about `virtual`, not about polymorphism: a class that genuinely is
  an interface should be entirely virtual, per
  [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md).
- Marking a virtual function `final` is a different remedy for a related concern
  and carries its own caution.

## Enforcement

- Flag a class with virtual functions but no derived classes.
- Flag a class where all member functions are virtual and have implementations.

## Related

- [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md)
  — do not build a hierarchy without a reason either.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — where all-virtual is right.
- [0100](0100-c-122-use-abstract-classes-as-interfaces-when-complete-separation-of-interface-and-implementation-is-needed.md)
  — the reason that justifies the cost.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — a redundant `virtual` wastes
  both.
