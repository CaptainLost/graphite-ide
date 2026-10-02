# 0418. NR.7: Don't make all data members protected

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. This record adopts the rejection, not
the practice being rejected.

The practice here is declaring a base class's data members `protected` as a
matter of course, so that derived classes can reach them. `protected` data is a
source of errors: it can be manipulated from an unbounded amount of code in
various places, since any class that derives from the base — including classes
not yet written, in other libraries — obtains write access to it. `protected`
data is the class-hierarchy equivalent of global data.

The alternative upstream offers is to make member data public or, preferably,
private.

Upstream marks this rule's example as a placeholder, so there is no code to
transcribe.

## Decision

Do not make data members `protected` by default, and do not accept a base class
that exposes its representation to its derived classes.

- Make data members private. A base class's representation is its own, and a
  derived class is not entitled to it merely by deriving.
- Where a derived class needs to observe or change base state, provide a
  `protected` member function that enforces the invariant, and keep the data
  private behind it.
- Where the members can vary independently and there is no invariant to protect,
  make them public and use a `struct`, per
  [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md);
  `protected` is not a middle ground between the two, it is the worst of both.
- Do not add `protected` data to make a hierarchy convenient to extend; that
  convenience is precisely the unbounded access the rule warns about.

## Rationale

The comparison with global data is the argument. An invariant can only be
maintained by code that is known and finite; `protected` makes the set of code
that can break the invariant open-ended, because deriving is something any
future translation unit can do without the base class's author being consulted.
Private data with a `protected` accessor keeps the same access and keeps the
set of writers to the base class itself.

The rule is also about defaults. Making everything `protected` in advance
decides the question for members nobody has yet needed, and once a derived class
depends on one it cannot be taken back.

## Consequences

### Positive

- The code that can break a class's invariant is bounded by the class, so the
  invariant can actually be reasoned about.
- The base's representation can be changed without auditing every derived class
  in every dependent library.
- Each member's access level becomes a decision, made when there is a case for
  it, rather than a blanket setting.

### Negative

- A `protected` accessor pair for state a derived class legitimately needs is
  more code than the member would have been, and inlined trivial accessors are
  the kind of boilerplate
  [0106](0106-c-131-avoid-trivial-getters-and-setters.md) warns about.
- Implementation inheritance sometimes genuinely wants shared mutable state, and
  the rule offers no positive answer for that case beyond narrowing the
  interface.
- Retrofitting it on an existing hierarchy is a breaking change to every derived
  class, so the rule is much cheaper to apply at design time than later.
- The rule targets the default, not the construct; when `protected` data is
  actually justified is a judgement left open.

### Neutral

- `protected` member *functions* are not what this rule objects to; they are the
  recommended way to give derived classes controlled access.
- The rule says nothing about whether the hierarchy should exist. A class with
  no derived classes has no `protected` question to answer.

## Related

- [0108](0108-c-133-avoid-protected-data.md) — the same rule stated positively,
  with its own reasoning.
- [0047](0047-c-9-minimize-exposure-of-members.md) — the general principle this
  is an instance of.
- [0109](0109-c-134-ensure-all-non-const-data-members-have-the-same-access-level.md)
  — the related constraint on mixing access levels.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the global-data
  objection that `protected` data reproduces inside a hierarchy.
