# 0109. C.134: Ensure all non-const data members have the same access level

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reason is prevention of logical confusion leading to errors. If the
non-`const` data members do not have the same access level, the type is confused
about what it is trying to do: is it a type that maintains an invariant, or
simply a collection of values?

The core question is: what code is responsible for maintaining a meaningful and
correct value for that variable? There are exactly two kinds of data member.
Category A are those that do not participate in the object's invariant — any
combination of values for these members is valid. Category B are those that do
participate in the object's invariant — not every combination of values is
meaningful, or there would be no invariant, and therefore all code with write
access to these variables must know about the invariant, know the semantics, and
actively implement and enforce the rules for keeping the values correct.

Data members in category A should just be public, or more rarely protected if
only derived classes should see them. They do not need encapsulation; all code
in the system might as well see and manipulate them.

Data members in category B should be private or `const`. Encapsulation matters
here: making them non-private and non-`const` would mean the object cannot
control its own state. An unbounded amount of code beyond the class would need
to know about the invariant and participate in maintaining it accurately — if
the members were public, that would be all calling code that uses the object; if
protected, all the code in current and future derived classes. This leads to
brittle and tightly coupled code that quickly becomes a nightmare to maintain.
Any code that inadvertently set the members to an invalid or unexpected
combination of values would corrupt the object and every subsequent use of it.

Most classes are therefore either all A or all B. All public: an aggregate
bundle of variables with no invariant across them, in which case all the
variables should be public, and by convention such a type is declared `struct`
rather than `class`. All private: a type that maintains an invariant, in which
case all the non-`const` variables should be private.

The exception is that classes occasionally mix A and B, usually for debugging.
An encapsulated object might contain non-`const` debug instrumentation that is
not part of the invariant, and so falls into category A — it is not really part
of the object's value or its meaningful observable state either. In that case
the A parts should be treated as A's, made public or in rarer cases protected if
they should be visible only to derived classes, and the B parts should still be
treated like B's, private or `const`.

## Decision

Give all non-`const` data members of a class the same access level.

- Decide first whether the type has an invariant across its members.
- If it does not, make every data member public and declare the type `struct`.
- If it does, make every non-`const` data member private, and expose the state
  through member functions that maintain the invariant.
- Do not split the members between public and private according to which ones
  happen to need protecting today; that split is the signal that the type is two
  types.
- Where debug or instrumentation state genuinely does not participate in the
  invariant, it may be public alongside private invariant state, and it should
  be recognisable as instrumentation.
- `const` members are exempt, because nothing can write to them after
  construction and so no code shares responsibility for their value.

## Rationale

The rule is really a diagnostic. Mixed access levels do not cause a bug
directly; they are evidence that the author has not decided whether the type
guards something or merely carries something, and every later reader has to
re-derive that decision from the member list.

The framing in terms of responsibility is what makes it actionable.
Encapsulation is not about hiding, it is about bounding the set of code that
must understand the invariant. A public non-`const` member widens that set to
the whole program; a protected one widens it to every derived class that will
ever exist. Neither set can be reviewed.

## Consequences

### Positive

- Whether a type is an aggregate or an invariant-keeping abstraction is visible
  from its member list.
- The code responsible for each member's correctness is bounded and reviewable.
- The `struct`/`class` convention that follows gives the reader the same
  information at the declaration.

### Negative

- Types that legitimately mix guarded and unguarded state — a cache, a dirty
  flag, an instrumentation counter — have to either bury the unguarded members
  behind accessors, which
  [0106](0106-c-131-avoid-trivial-getters-and-setters.md) argues against, or
  take the exception and mix the levels after all.
- Making everything private in an invariant-keeping type forces accessors for
  members that carry no invariant at all, which is exactly the trivial-accessor
  cost.
- The rule is silent on `mutable` members and on static data, both of which
  raise the same responsibility question.

### Neutral

- `const` members may sit at any access level, and commonly are public.
- The debug-instrumentation exception is narrow and explicitly identified as
  such.

## Enforcement

- Flag any class that has non-`const` data members with different access levels.

## Related

- [0108](0108-c-133-avoid-protected-data.md) — the protected case, rejected
  outright.
- [0106](0106-c-131-avoid-trivial-getters-and-setters.md) — the rule that
  handles category A types by making them `struct`s.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — a `const` member
  is exempt because it cannot be written.
- [0024](0024-i-5-state-preconditions-if-any.md) — an invariant is the class-level
  form of the same idea.
