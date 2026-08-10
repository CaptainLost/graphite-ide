# 0430. NL.16: Use a conventional class member declaration order

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

A conventional order of members improves readability, because a reader looking
for a particular kind of member knows where to look without reading the whole
class. The order upstream gives is: types — classes, enumerations and `using`
aliases — then constructors, assignment operators and the destructor, then the
remaining functions, then data. Access sections run public before protected
before private.

```cpp
class X {
public:
    // interface
protected:
    // unchecked function for use by derived class implementations
private:
    // implementation details
};
```

The two orderings sometimes disagree, because separating the public interface
from the implementation details conflicts with grouping all types first. Where
they do, private types and functions can be placed with the private data:

```cpp
class X {
public:
    // interface
protected:
    // unchecked function for use by derived class implementations
private:
    // implementation details (types, functions, and data)
};
```

What is to be avoided is scattering: multiple blocks of one access dispersed
among blocks of another.

```cpp
class X {   // bad
public:
    void f();
public:
    int g();
    // ...
};
```

Upstream also notes that macros declaring groups of members tend to break any
ordering rule — though it adds that using macros obscures what is being
expressed in any case. Upstream marks this as a recommendation for when there
are no constraints or better ideas.

## Decision

Declare members in a conventional order.

- Order the access sections public, then protected, then private, and give each
  access exactly one section.
- Within a class, order members: nested types and aliases, then constructors,
  assignment operators and the destructor, then other member functions, then
  data.
- Where the two orderings conflict, prefer separating interface from
  implementation: private types and private helper functions go with the private
  data.
- Do not use a macro to declare a group of members.
- Follow the order the file already uses when adding to an existing class; a
  reordering is a separate change from an addition.

## Rationale

The order is worth having only because it is the same everywhere: a reader who
has learnt it stops searching for the data members and simply looks at the
bottom. Its second effect is more useful than the first — public first means the
part of the class that other code depends on is the part you meet first, and the
implementation details are where a reader can stop.

## Consequences

### Positive

- The interface is at the top, so the class can be understood without reading to
  the end.
- A single section per access makes it impossible to miss a member that was
  declared in a second, later block of the same access.
- Data members grouped at the end make the object's size and layout visible in
  one place, which matters when initialisation order is being checked.

### Negative

- The rule conflicts with itself, as upstream admits: types-first and
  interface-first cannot both be satisfied when a private nested type exists, and
  the resolution offered is a preference rather than a rule.
- It fights cohesion. Keeping a small helper function next to the data it
  maintains often reads better than pushing every function above every data
  member, and this rule forbids it.
- Enforcement is coarse — upstream expects a great deal of existing code not to
  comply — so in practice the rule is applied to new classes and asserted about
  old ones.
- A formatter cannot help. Layout tools position access labels and indentation
  but have no opinion about the order of what follows them, so the rule survives
  on review alone.

### Neutral

- Declaring a supporting type at namespace scope, alongside the class that uses
  it rather than nested inside it, sidesteps the types-first question rather than
  answering it, and is not a departure from the rule.
- Where a member goes is unrelated to whether it should exist; minimising the
  private surface is a different rule.

## Enforcement

Flag departures from the order above. Expect a substantial amount of older code
not to follow it.

## Related

- [0047](0047-c-9-minimize-exposure-of-members.md) — what belongs in the private
  section at all, before this rule decides where it goes.
- [0109](0109-c-134-ensure-all-non-const-data-members-have-the-same-access-level.md)
  — data members with mixed access make a single private section impossible.
- [0046](0046-c-8-use-class-rather-than-struct-if-any-member-is-non-public.md) —
  the choice of `class` or `struct` decides whether access sections appear at
  all.
- [0068](0068-c-47-define-and-initialize-member-variables-in-the-order-of-member-declaration.md)
  — declaration order of data has a semantic consequence, not only a readability
  one.
