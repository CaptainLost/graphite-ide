# 0292. E.4: Design your error-handling strategy around invariants

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

To use an object it must be in a valid state, defined formally or informally by
an invariant; and to recover from an error, every object that is not destroyed
must be in a valid state.

An invariant is a logical condition for the members of an object that a
constructor must establish for the public member functions to assume. It is the
contract between the constructor and every other member function: the
constructor's job is to make it true, each member function may assume it on
entry and must leave it true on exit, and the destructor is the only operation
entitled to dismantle it.

That is why invariants are the right axis for an error-handling strategy rather
than an unrelated concern. "Recovering" from an error means continuing to run,
and continuing to run means touching objects that were alive when the failure
happened. Whether that is safe is exactly the question of whether their
invariants still hold. A strategy that says how errors travel but not what state
they leave behind has answered the easy half.

Organising around invariants also decides where checks belong. If a class's
invariant says a handle is either null or open, no member function needs to
re-derive that; it checks its own preconditions and trusts the invariant. If the
invariant is not written down, every member function ends up defending against
states the constructor already excluded, and the defensive code becomes the bulk
of the class.

The invariant also draws the line between the two kinds of bad news a program
encounters. A condition the environment can produce — a file that is not there,
a connection refused — leaves every object's invariant intact and is an error to
be reported. A violated invariant is a bug: the code that was supposed to
maintain it did not, and no caller can act on that.

## Decision

Organise error handling around what must remain true of each object.

- State each class's invariant, at minimum as a comment on the class, and let
  the constructors be the code that establishes it.
- Let member functions assume the invariant rather than re-checking it, and
  require each to restore it before returning.
- Decide, for every operation that can fail, which objects are left in a valid
  state and say so. An operation that can leave an object neither valid nor
  destroyed is a design defect, not an implementation detail.
- Treat a broken invariant as a programmer error, distinct from an environmental
  failure, and do not route the two through the same channel. The first is a bug
  to be caught; the second is a condition to be reported.
- Where an operation cannot preserve an invariant on failure, give it a
  narrower contract — operate on a copy and commit, or split it into a step that
  can fail and a step that cannot.

## Rationale

Error handling and object validity are the same question asked twice. The
mechanism decides where control goes; the invariant decides whether the code it
arrives at may touch anything. Only the second determines whether the program is
still correct.

The practical payoff is that invariants tell you which failures need no handling
at all. If a class cannot be constructed in a broken state, the whole family of
"is this object usable?" checks disappears from its callers — which is the same
economy [0293](0293-e-5-let-a-constructor-establish-an-invariant-and-throw-if-it-cannot.md)
argues for from the constructor's side.

## Consequences

### Positive

- Failure paths can be reasoned about locally: the question is always whether
  this object's invariant still holds.
- Member functions shrink, because they assume the invariant instead of
  defending against its absence.
- The distinction between a bug and an environmental failure gets a principled
  basis rather than a per-call-site guess.

### Negative

- Writing invariants down is work with no immediate return, and comments
  describing them drift out of date silently — nothing compiles them.
- Strong guarantees are expensive. Preserving an invariant across a failure
  often means building a complete new state and swapping, which costs an
  allocation and a copy the direct mutation would not have.
- Some types genuinely have weak invariants — a plain aggregate of independent
  fields — and forcing the vocabulary onto them produces ceremony without
  insight.
- Moved-from objects sit awkwardly in this scheme: valid but unspecified is a
  state the invariant must be written to admit, and it is easy to forget.

### Neutral

- The rule says organise around invariants, not which guarantee to offer. Basic
  and strong guarantees are both compatible with it.
- Invariants can be expressed as comments, asserts, or private constructors; the
  rule does not choose.

## Related

- [0293](0293-e-5-let-a-constructor-establish-an-invariant-and-throw-if-it-cannot.md)
  — the constructor's half of the contract.
- [0289](0289-e-1-develop-an-error-handling-strategy-early-in-a-design.md) — the
  strategy this record supplies the axis for.
- [0024](0024-i-5-state-preconditions-if-any.md) and
  [0026](0026-i-7-state-postconditions.md) — the per-function analogue of a class
  invariant.
