# 0213. ES.63: Don't slice

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Slicing — copying only part of an object using assignment or initialisation —
most often leads to errors, because the object was meant to be considered as a
whole. In the rare cases where the slicing is deliberate, the code is
surprising.

```cpp
class Shape { /* ... */ };
class Circle : public Shape { /* ... */ Point c; int r; };

Circle c { {0, 0}, 42 };
Shape s {c};    // copy construct only the Shape part of Circle
s = c;          // or copy assign only the Shape part of Circle

void assign(const Shape& src, Shape& dest)
{
    dest = src;
}

Circle c2 { {1, 1}, 43 };
assign(c, c2);   // oops, not the whole state is transferred
assert(c == c2); // if we supply copying, we should also provide comparison,
                 // but this will likely return false
```

The result is meaningless, because the centre and the radius are not copied from
`c` into `s`. The `assign` case is worse than the direct one: both arguments are
whole `Circle`s at the call site, the function looks entirely reasonable, and
the slicing happens inside a function that never mentions `Circle`.

The first defence against this is to define the base class so that it does not
allow copying at all. Where slicing really is intended, define an explicit
operation for it, which saves readers from confusion:

```cpp
class Smiley : public Circle {
    public:
    Circle copy_circle();
    // ...
};

Smiley sm { /* ... */ };
Circle c1 {sm};  // ideally prevented by the definition of Circle
Circle c2 {sm.copy_circle()};
```

## Decision

Do not copy or assign a polymorphic object through a base-class type.

- Suppress copy and move on any class used as a base, so slicing cannot compile;
  see
  [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md).
- Do not declare a parameter, a local, or a container element by a base class
  value type. Access polymorphic objects through references or pointers, per
  [0116](0116-c-145-access-polymorphic-objects-through-pointers-and-references.md).
- Do not write `Base& dest = ...; dest = src;` for polymorphic `Base`. Assigning
  through a base reference is the same defect as constructing a base value, and
  it is much harder to see.
- Where a base-class copy really is wanted, give it a name — `copy_circle()`,
  or a virtual `clone()` for the whole-object case — so the intent is written
  down rather than inferred.
- Where an object needs to be duplicated polymorphically, use a virtual clone
  function, per
  [0105](0105-c-130-for-making-deep-copies-of-polymorphic-classes-prefer-a-virtual-clone-function-instead-of-public-copy-construction-assignment.md).

## Rationale

Slicing is not a wrong copy; it is a successful copy of a different object. The
program continues with a `Shape` that is well formed, satisfies its own
invariants, and has silently lost the state that made it a `Circle`. Nothing
downstream can detect that anything happened.

The `assign` function is why this cannot be handled by care at the call site.
The call `assign(c, c2)` passes two complete objects and gets back a partial
result, and neither the call nor the function body contains anything a reviewer
could point at. The only place the defect can be stopped is in the definition of
the base class, which is why the decision above is mostly about class design
rather than about expressions.

## Consequences

### Positive

- The most common way to lose derived state accidentally becomes a compile
  error rather than a silent behaviour change.
- Deliberate slicing acquires a name, so the reader learns that a partial copy
  was intended.
- Suppressing base-class copy also removes the related trap of an assignment
  operator that is not virtual and cannot be made to work polymorphically.

### Negative

- The rule cannot be followed by the code that violates it. Fixing it means
  changing the base class, which may be third-party, may have many users, and
  may deliberately be a copyable value type in some of them.
- Suppressing copy and move on a base class also removes the copy that derived
  classes need in their own copy operations, so those must be written or
  defaulted explicitly, per
  [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md).
- The `assert(c == c2)` line in the example hints at a second obligation the
  rule does not resolve: a hierarchy that supplies copying should supply
  comparison, and comparison across a polymorphic hierarchy is its own hard
  problem.
- "Warn against slicing" as an enforcement produces false positives on
  deliberate value-semantics hierarchies, so it cannot simply be left on as an
  error.

### Neutral

- Passing a derived object by `const Base&` is not slicing and remains correct;
  the defect appears only when a base-typed object is constructed or assigned.
- Concrete types with no derived classes are unaffected, and remain the
  preferred default under
  [0048](0048-c-10-prefer-concrete-types-over-class-hierarchies.md).

## Enforcement

Warn against slicing.

## Related

- [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) —
  the class-side fix that makes slicing impossible.
- [0116](0116-c-145-access-polymorphic-objects-through-pointers-and-references.md)
  — the access rule that keeps base-typed values from being created.
- [0105](0105-c-130-for-making-deep-copies-of-polymorphic-classes-prefer-a-virtual-clone-function-instead-of-public-copy-construction-assignment.md)
  — the named operation to use when a polymorphic copy is wanted.
- [0048](0048-c-10-prefer-concrete-types-over-class-hierarchies.md) — the design
  in which this question never arises.
