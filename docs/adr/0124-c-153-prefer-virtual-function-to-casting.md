# 0124. C.153: Prefer virtual function to casting

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A virtual function call is safe, whereas casting is error-prone. A virtual
function call reaches the most derived function; a cast might reach an
intermediate class and therefore give a wrong result, especially as a hierarchy
is modified during maintenance.

The upstream rule carries no example. The asymmetry it describes is between two
ways of asking a hierarchy to do the right thing. A virtual call asks the
object, and the answer is computed from what the object is, at the moment of the
call, by the language. A cast asks the author, and the answer is whatever the
author believed when the line was written — which is why the two behave
differently under maintenance. Insert a class between base and derived, or move
an override one level up or down, and every virtual call site continues to reach
the most derived implementation while a downcast to the class that used to
provide the behaviour now succeeds and reaches the wrong one.

Where hierarchy navigation genuinely cannot be avoided, the cast must at least
be checked; that is
[0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md).
This rule is the one that comes first.

## Decision

Reach behaviour through virtual dispatch rather than by discovering a type.

- When code branches on which derived class an object is, in order to call
  something, add a virtual function to the base and move each branch into the
  corresponding override.
- Do not introduce a downcast to gain access to an operation; that operation
  belongs on the interface the caller already holds.
- Where the base cannot reasonably carry the operation, consider a visitor, or a
  closed set of alternatives held in a `variant`, before a chain of casts.
- Treat an accumulating sequence of `dynamic_cast` tests as evidence that the
  base interface is missing something, not as a pattern to extend.
- Where a cast survives all of that, it is a checked one, and it is confined
  behind a named function rather than repeated at each call site.

## Rationale

The two constructs differ in who holds the type knowledge. Virtual dispatch
keeps it in one place, next to the class it describes, where the person adding a
derived class is obliged to consider it. A cast copies that knowledge out to
every call site, where it silently decays as the hierarchy changes and where
nothing forces an author extending the hierarchy to visit it. That is why the
failure mode is delayed: the cast that reaches an intermediate class was correct
when it was written.

## Consequences

### Positive

- Adding a derived class requires implementing the operation, rather than hoping
  every existing cast site is revisited.
- Call sites shrink to a single call, with no type test and no null path.
- Behaviour and data stay together in the class, which is what makes the
  hierarchy worth having.

### Negative

- Every operation moved onto the base widens the base interface, which pulls
  directly against keeping an interface minimal and against
  [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md);
  a base that acquires a virtual function per caller is its own design failure.
- The base must depend on whatever the operation needs, which can drag
  dependencies down into a class that had no business knowing about them.
- Operations that apply to only some derived classes, or that involve two
  objects at once, do not fit virtual dispatch, and forcing them in produces
  overrides that do nothing or report failure.
- A virtual call is an indirect call and cannot generally be inlined, so the
  substitution is not free in a hot path.

### Neutral

- The rule expresses a preference, not a prohibition; it presumes that
  navigation is sometimes unavoidable and hands that case to the
  `dynamic_cast` rule.
- Nothing here decides how the hierarchy should be shaped, only how existing
  behaviour should be reached.

## Enforcement

- No direct check exists. The mechanical part is the enforcement of
  [0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md):
  flag `static_cast` and C-style downcasts, which makes the remaining
  navigation visible for review.

## Related

- [0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md)
  — what to do when this rule cannot be followed.
- [0115](0115-c-140-do-not-provide-different-default-arguments-for-a-virtual-function-and-an-overrider.md)
  — virtual dispatch is only trustworthy while the whole call follows the
  object.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the counterweight: the base interface must not grow without limit.
- [0009](0009-p-3-express-intent.md) — a virtual call says what is wanted; a
  cast says what the author thinks the object is.
