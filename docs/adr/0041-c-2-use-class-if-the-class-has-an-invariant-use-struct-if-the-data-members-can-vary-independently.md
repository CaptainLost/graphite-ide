# 0041. C.2: Use class if the class has an invariant; use struct if the data members can vary independently

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Readability and ease of comprehension. The use of `class` alerts the programmer
to the need for an invariant. This is a useful convention, and it is only a
convention: the language itself distinguishes the two keywords by default member
visibility and nothing else.

An invariant is a logical condition over the members of an object that a
constructor must establish, and which the public member functions may then
assume. Once the invariant is established — typically by a constructor — every
member function can be called on the object. An invariant can be stated
informally in a comment or more formally with `Expects`. If all data members can
vary independently of each other, no invariant is possible, and there is nothing
for a `class` to protect.

```cpp
struct Pair {  // the members can vary independently
    string name;
    int volume;
};
```

but:

```cpp
class Date {
public:
    // validate that {yy, mm, dd} is a valid date and initialize
    Date(int yy, Month mm, char dd);
    // ...
private:
    int y;
    Month m;
    char d;    // day
};
```

Any name and any volume together make a valid `Pair`; not every triple of
numbers makes a valid `Date`.

If a class has any private data, a user cannot completely initialize an object
without a constructor. The definer therefore has to provide one and specify what
it means, which in effect means defining an invariant.

## Decision

Choose the keyword by asking whether the members are constrained relative to one
another.

- Write `struct` when every member can take any value of its type independently
  of the others, and leave the members public.
- Write `class` when some combination of member values is invalid, make the
  constrained members private, and establish the condition in a constructor.
- State the invariant where the reader will find it: as a comment on the class,
  or as `Expects`/`Ensures` on the operations that assume it, per
  [0024](0024-i-5-state-preconditions-if-any.md) and
  [0026](0026-i-7-state-postconditions.md).
- Do not write a `struct` with all its data private, and do not write a `class`
  whose data is all public. Either is the keyword contradicting the design.

## Rationale

The keyword is a one-word summary of the design, read before any member is read.
`class` tells the reader that arbitrary member values are not permitted and that
some function is responsible for that; `struct` tells them no such function
exists and none is needed. Getting this backwards costs nothing at compile time
and everything in comprehension.

The link to constructors is what makes the rule more than cosmetic: private data
forces a constructor, a constructor forces a decision about what a valid object
is, and that decision is the invariant. Choosing `class` is therefore the moment
the invariant gets thought about at all.

## Consequences

### Positive

- The reader learns whether the type has an invariant from its first line.
- Choosing `class` forces the invariant to be named and established somewhere.
- Aggregate initialisation stays available for the types that genuinely do not
  need protecting.

### Negative

- The distinction is a convention with no compiler backing, so it degrades
  silently the moment someone ignores it, and a wrong keyword produces no
  diagnostic at all.
- Invariants appear over time. A `struct` that acquires a constraint has to be
  converted to a `class`, which is a breaking change for every aggregate
  initialisation and designated initialiser at its call sites.
- Some types sit on the line — a member that is merely conventionally non-empty,
  or an enum whose values are all legal but not all sensible — and the rule
  gives no way to decide those.

### Neutral

- Access defaults are the only real difference between the keywords; everything
  else here is communication.
- A `class` may still expose members that are genuinely unconstrained, subject
  to [0047](0047-c-9-minimize-exposure-of-members.md).

## Enforcement

- Look for `struct`s with all data private, and `class`es with public data
  members. Either is a keyword that disagrees with its own contents.

## Related

- [0046](0046-c-8-use-class-rather-than-struct-if-any-member-is-non-public.md) —
  the same choice stated from the access side.
- [0047](0047-c-9-minimize-exposure-of-members.md) — how much of a `class`
  should be visible once the invariant exists.
- [0061](0061-c-40-define-a-constructor-if-a-class-has-an-invariant.md) — the
  invariant this record identifies is the one that constructor establishes.
- [0040](0040-c-1-organize-related-data-into-structures-structs-or-classes.md) —
  the grouping decision that comes first.
