# 0053. C.22: Make default operations consistent

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The default operations are conceptually a matched set and their semantics are
interrelated. Users will be surprised if copy or move construction and copy or
move assignment do logically different things. They will be surprised if the
constructors and the destructor do not provide a consistent view of resource
management. They will be surprised if copy and move do not reflect the way the
constructors and destructor work.

```cpp
class Silly {   // BAD: Inconsistent copy operations
    class Impl {
        // ...
    };
    shared_ptr<Impl> p;
public:
    Silly(const Silly& a) : p(make_shared<Impl>()) { *p = *a.p; }   // deep copy
    Silly& operator=(const Silly& a) { p = a.p; }   // shallow copy
    // ...
};
```

These operations disagree about copy semantics. Construct-from produces an
independent object; assign-from produces a shared one. Whether a later write
through one `Silly` is visible through another therefore depends on which
operation happened to be used, which no caller can be expected to track. This
leads to confusion and to bugs.

## Decision

Make the six operations describe one coherent story about the object's value and
its resources.

- Copy construction and copy assignment must produce the same relationship to
  the source. If one performs a deep copy, so must the other; if one shares, so
  must the other.
- Move construction and move assignment must agree with each other in the same
  way, and must leave the source in the same valid state.
- Every member that a copy or move constructor writes must be initialised by
  every other constructor, so that no construction path leaves a member the
  other paths set.
- If a copy or move constructor deep-copies a member, the destructor must
  release that member, and conversely: anything the destructor releases must be
  handled by the copy and move operations.
- Assignment should end with the object in a state indistinguishable from one
  constructed the same way.

## Rationale

Inconsistency here is worse than a missing operation, because a missing
operation is a compile error and an inconsistent one is a silent behavioural
difference that depends on syntax the caller chose for unrelated reasons.
`Silly x = y;` and `Silly x; x = y;` are the same intent written two ways, and a
type that distinguishes them is a type nobody can use correctly.

The resource half of the rule is really a consistency check on the design: the
destructor is a statement of what the object owns, and any copy operation that
disagrees with that statement is either leaking or double-freeing. Reading the
destructor and the copy constructor side by side is the cheapest available
review of a resource-owning class.

## Consequences

### Positive

- The behaviour of an object does not depend on which of two equivalent
  syntaxes created it.
- Ownership becomes reviewable by comparing the constructor set against the
  destructor.
- Later maintenance has a rule to check a new member against: it must appear in
  every constructor, in copy and move, and in the destructor if it needs
  releasing.

### Negative

- The consistency requirement is genuinely hard to check, and the upstream
  enforcement bullets are all marked complex for that reason. In practice it
  rests on review.
- It can force duplication: the same member-writing logic appears in the copy
  constructor and the copy assignment operator, and factoring it out into a
  private helper adds a function whose only purpose is to satisfy the rule.
- Move operations are legitimately allowed to leave the source in a different
  state from copy, so "consistent" cannot mean "identical", and where the line
  falls is a judgement.

### Neutral

- The rule constrains the relationships among the operations, not what any one
  of them does.
- It applies whether the operations are user-written or defaulted; defaulted
  ones are consistent automatically, which is another argument for
  [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md).

## Enforcement

- (Complex) A copy or move constructor and the corresponding copy or move
  assignment operator should write to the same member variables at the same
  level of dereference.
- (Complex) Any member variable written in a copy or move constructor should
  also be initialised by all other constructors.
- (Complex) If a copy or move constructor performs a deep copy of a member
  variable, the destructor should modify that member variable.
- (Complex) If a destructor modifies a member variable, that member variable
  should be written in any copy or move constructor or assignment operator.

## Related

- [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md)
  — declaring the whole set is the precondition for making it consistent.
- [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) —
  generated operations are consistent by construction.
- [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md)
  — the resource half of the consistency check.
- [0049](0049-c-11-make-concrete-types-regular.md) — regularity is this
  consistency extended to equality.
