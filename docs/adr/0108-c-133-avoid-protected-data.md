# 0108. C.133: Avoid protected data

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Protected data is a source of complexity and errors. It complicates the
statement of invariants, and it inherently violates the guidance against putting
data in base classes, which usually leads to having to deal with virtual
inheritance as well.

```cpp
// bad
class Shape {
public:
    // ... interface functions ...
protected:
    // data for use in derived classes:
    Color fill_color;
    Color edge_color;
    Style st;
};
```

Now it is up to every derived `Shape` to manipulate the protected data
correctly. This has been popular, but has also been a major source of
maintenance problems. In a large class hierarchy the consistent use of protected
data is hard to maintain, because there can be a lot of code spread over a lot
of classes. The set of classes that can touch the data is open: anyone can
derive a new class and start manipulating it. Often it is not possible to
examine the complete set of classes, so any change to the representation becomes
infeasible. There is no enforced invariant for protected data; it is much like a
set of global variables. The protected data has de facto become global to a
large body of code.

Protected data often looks tempting as a way to enable arbitrary improvements
through derivation. Often, what you get is unprincipled changes and errors.
Prefer private data with a well-specified and enforced invariant. An
alternative, and often better, approach is to keep data out of any class used as
an interface.

Protected *member functions* can be just fine. The rule is about state, not
about access control in general: a protected function is a controlled operation
offered to derived classes, whereas protected data is unrestricted write access
to the base's representation.

## Decision

Do not declare protected data members.

- Make data members `private`, and give the base a well-specified invariant it
  enforces itself.
- Where derived classes need to act on the base's state, offer protected member
  functions that perform the operation and preserve the invariant, rather than
  exposing the state.
- Where derived classes only need to read the state, offer a protected or public
  accessor that returns it.
- Where the base is an interface, keep the data out of it entirely and let each
  implementation hold its own, per
  [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md).
- Where a genuinely shared implementation base is warranted, keep its data
  private to it and let its derived classes go through its functions.

## Rationale

The comparison to global variables is the argument, and it is exact. The set of
code that may write a private member is bounded by the class definition; the set
that may write a protected member is bounded by nothing, because a new derived
class can be added by anyone at any time, possibly in another library. Neither
set can be enumerated for the protected case, so the representation can never be
changed with confidence.

The consequence is that a protected member has no invariant that anyone can
state, let alone enforce. A protected function, by contrast, is a fixed number
of operations the base has approved — which is why the rule permits them.

## Consequences

### Positive

- The base can change its representation, because the set of writers is closed.
- An invariant over the base's state can be stated and enforced in one place.
- Derived classes are coupled to the base's operations, not to its layout.

### Negative

- Every access derived classes need becomes a function call to write, so a base
  with much shared state grows a wide protected function surface — which is
  itself an interface that must be maintained.
- Going through functions can cost an indirection where direct member access
  would not, and in a hot path that occasionally matters.
- The rule assumes shared state in a base is avoidable; where a design genuinely
  needs it, the rule converts a visibility problem into an API design problem
  rather than removing it.

### Neutral

- Protected member functions are explicitly fine, and are the recommended
  substitute.
- `protected` on a non-const member is also caught by
  [0109](0109-c-134-ensure-all-non-const-data-members-have-the-same-access-level.md)
  when it is mixed with private data.

## Enforcement

- Flag classes with protected data.

## Related

- [0109](0109-c-134-ensure-all-non-const-data-members-have-the-same-access-level.md)
  — the neighbouring rule about who is responsible for a member's value.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the stronger form: keep data out of an interface base altogether.
- [0104](0104-c-129-when-designing-a-class-hierarchy-distinguish-between-implementation-inheritance-and-interface-inheritance.md)
  — protected data is how the two kinds of inheritance get mixed.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the comparison the rule
  itself draws.
