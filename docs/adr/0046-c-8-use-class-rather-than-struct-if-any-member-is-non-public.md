# 0046. C.8: Use class rather than struct if any member is non-public

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Readability. Writing `class` makes it clear that something is being hidden or
abstracted. This is a useful convention, and like most conventions its value
comes from being followed without exception.

```cpp
struct Date {           // bad
    int d, m;
    Date(int i, Month m);
    // ... lots of functions ...
private:
    int y;  // year
};
```

There is nothing wrong with this code as far as the language rules are
concerned, but nearly everything is wrong with it from a design perspective. The
private data is hidden far from the public data. The data is split across
different parts of the class declaration. Different parts of the data have
different access. All of this decreases readability and complicates maintenance.

Prefer to place the interface first in a class: public members, then protected,
then private.

## Decision

Write `class` for any type that has a non-public member.

- If a type needs a `private:` or `protected:` section at all, declare it with
  the `class` keyword and let the default access do the work.
- Reserve `struct` for types where every member is public and no access
  specifier appears in the body.
- Order the sections public, then protected, then private, so the interface is
  read first and the representation last.
- Keep the data together in one section rather than scattering members across
  several access levels within one type.

## Rationale

The keyword is the first thing read and it should not contradict the twentieth
line of the body. A `struct` with a `private:` section forces the reader to
scan the whole declaration before they know whether the type is an aggregate,
which is exactly the question they were asking when they read the keyword.

The deeper complaint in the example is not the keyword but the layout it enables
— data at both ends of a long declaration, at two access levels, with the
functions in between. Choosing `class` prompts the author to gather the
representation into one place, which is the change that actually helps.

## Consequences

### Positive

- The keyword and the access structure agree, so the first line is reliable.
- The representation ends up in one place at the bottom of the declaration.
- The convention is mechanically checkable, unlike most readability rules.

### Negative

- Changing a `struct` to a `class` requires adding an explicit `public:` for
  everything that was implicitly public, so the mechanical fix is a diff across
  the whole type.
- The rule can be satisfied by editing one keyword while leaving the scattered
  layout that motivated it, which makes a checker's approval misleading.
- Templates and forward declarations must agree on the class-key with the
  definition in some contexts, so a late switch can ripple into unrelated
  headers.

### Neutral

- This is the access-side statement of the same choice
  [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md)
  makes on invariant grounds; in practice the two agree, because a private
  member is usually private in order to protect something.
- The language treats the keywords as interchangeable apart from default access
  and default inheritance access.

## Enforcement

- Flag classes declared with `struct` that contain a private or protected
  member.

## Related

- [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md)
  — the same decision argued from the invariant.
- [0047](0047-c-9-minimize-exposure-of-members.md) — the rule that produces the
  non-public members in the first place.
- [0042](0042-c-3-represent-the-distinction-between-an-interface-and-an-implementation-using-a-class.md)
  — interface first, representation last, is that boundary made visible in the
  declaration order.
