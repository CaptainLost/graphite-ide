# 0040. C.1: Organize related data into structures (structs or classes)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Ease of comprehension. If data is related for fundamental reasons, that fact
should be reflected in the code rather than left to the reader to reconstruct
from a parameter list.

```cpp
void draw(int x, int y, int x2, int y2);  // BAD: unnecessary implicit relationships
void draw(Point from, Point to);          // better
```

The first signature states four independent integers and hopes the caller knows
that the first two form one point and the last two another. The relationship is
real, but it exists only in the reader's head and in the comment, if there is
one. The second signature makes the relationship part of the type system, where
the compiler can see it too.

A simple class without virtual functions implies no space or time overhead, so
grouping is not paid for at run time. Two `int`s in a `Point` occupy exactly
what two loose `int`s occupy.

From a language perspective, `class` and `struct` differ only in the default
visibility of their members. Choosing between them is a question about
invariants, not about capability.

## Decision

Group data that is related for fundamental reasons into a `struct` or a `class`,
and pass and store the group as one object.

- When two or more parameters are always supplied together and mean nothing
  apart, give them a named type and take that type instead.
- Name the type after the concept, not after its representation: `Point`,
  `Extent`, `LineRange`, not `TwoInts`.
- Do not treat the grouping as expensive. A class with no virtual functions
  costs nothing beyond the members it holds.
- Choose `struct` or `class` on the invariant, per
  [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md),
  not on which keyword feels lighter.

## Rationale

The sharpest argument is the one the guideline makes about implicit
relationships: a relationship the code does not state is a relationship nothing
can check. Once `Point` exists, no caller can pass `y` where `x` was expected,
no future maintainer can reorder half of the pair, and every function that
handles a point handles it the same way.

Grouping also shortens argument lists, which is the concern of
[0034](0034-i-23-keep-the-number-of-function-arguments-low.md), and removes
adjacent same-typed parameters, which is the trap described in
[0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md).
One structural change answers three separate complaints.

## Consequences

### Positive

- The relationship between the data becomes checkable rather than conventional.
- Argument lists shrink, and adjacent interchangeable parameters disappear.
- A change to the group — adding a `z`, changing a unit — touches one type
  rather than every signature that spelled the members out.

### Negative

- It creates types, and every type is a header, a name to agree on, and a thing
  to keep consistent. A codebase can accumulate a `Point`, a `Position` and a
  `Coord` that all mean the same thing.
- Deciding what is "related for fundamental reasons" is a judgement, and
  grouping things that merely happen to travel together produces a type with no
  meaning of its own.
- Passing a group by value where the callee needed one member is marginally more
  work for the caller, and can obscure at the call site which member actually
  matters.

### Neutral

- The rule says nothing about how the group is stored, only that it exists as a
  type.
- Whether the group is a `struct` or a `class` is decided elsewhere.

## Enforcement

- Probably impossible to enforce mechanically. A heuristic looking for data
  items that are consistently used together may be possible, but nothing more
  reliable is known.

## Related

- [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md)
  — once the group exists, this decides which keyword it gets.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a named
  group is a stronger type than the integers it replaces.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — grouping is
  the principal way a long argument list gets shorter.
- [0009](0009-p-3-express-intent.md) — the relationship is intent, and intent
  belongs in the code.
