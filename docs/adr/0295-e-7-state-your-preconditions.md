# 0295. E.7: State your preconditions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

The reason given is short: to avoid interface errors. The rule appears in the
error-handling section rather than only in the interface section because a
precondition is what separates the errors a function is responsible for from the
ones its caller is responsible for, and that division has to be made before any
error-handling mechanism can be applied to it.

An unstated precondition produces a specific kind of bug. The callee assumes
something about an argument; the caller does not know it; the code works until
an input arrives that the author never tried. Nothing in the signature recorded
the assumption, so nothing in review or compilation could have caught the
mismatch. Upstream calls the result an interface error, and lists it among the
categories these rules exist to prevent.

A precondition is not an error condition. A function that is called with a
negative length is not failing to do its job — it was asked a question it never
promised to answer. The distinction decides which mechanism applies: an unmet
precondition is a bug in the caller, while a file that does not exist is a
condition the environment produced. Blurring the two produces functions that
return errors for programmer mistakes and, worse, callers that handle those
errors and carry on.

The concrete transcription of how to state a precondition — in the type system
where possible, as `Expects` where not, and as precise English on the
declaration where even that is impractical — is recorded at
[0024](0024-i-5-state-preconditions-if-any.md) and
[0025](0025-i-6-prefer-expects-for-expressing-preconditions.md).

## Decision

State every precondition a function has, and treat a violated one as a bug
rather than an error.

- Before choosing how a function reports failure, decide which conditions are
  its responsibility and which are the caller's. Only the first are errors.
- State the caller's obligations on the declaration: in the types where the
  types can carry them, otherwise as an assertion in the definition, otherwise
  as a precise comment.
- Do not report a violated precondition through the error channel. A caller
  cannot recover from its own bug, so handing it a recoverable-looking failure
  invites it to carry on.
- Do not silently accept an argument that violates a precondition by giving it a
  fallback meaning. That converts a caller's bug into a wrong answer.
- Do not restate a class invariant as a precondition on each member function.

## Rationale

Preconditions are error handling's boundary condition: they say where the
function's obligations begin, and therefore which failures its error channel is
for. A function with no stated preconditions has implicitly promised to cope
with everything, and no error mechanism can make that promise true.

Keeping precondition violations out of the error channel also keeps the channel
meaningful. If a reported failure can mean "the disk was full" or "you passed me
a null pointer", a caller that handles the failure is handling both, and the
second was never recoverable.

## Consequences

### Positive

- The error channel carries only conditions a caller can sensibly act on.
- Callers can see what they are responsible for without reading the callee.
- Programmer errors fail loudly rather than being folded into a
  recoverable-looking result.

### Negative

- Assertions are conventionally compiled out in release builds, so a
  precondition checked only by assertion is unchecked exactly where it would do
  most good.
- Deciding whether a condition is a precondition or an error is a judgement, and
  the same condition can be either depending on the caller — a path that must
  exist for one caller is a question for another.
- Comments stating preconditions are not checked by anything and drift.
- The rule adds text to every declaration that has an obligation, and most do.
- The guidelines give E.7 one sentence of Reason — "to avoid interface errors" —
  and a pointer to the interface-section rule. The error-handling framing in
  this record is reasoned from that sentence rather than transcribed, so it
  cannot be checked against upstream argument; the transcribed substance is at
  [0024](0024-i-5-state-preconditions-if-any.md).

### Neutral

- Where a precondition can be expressed in the type system, stating it is free
  and this record has nothing further to ask.
- The mechanism for stating one is not decided here; see
  [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md).

## Related

- [0024](0024-i-5-state-preconditions-if-any.md) — the full transcription of the
  precondition rule, with examples.
- [0025](0025-i-6-prefer-expects-for-expressing-preconditions.md) — how to state
  one formally.
- [0296](0296-e-8-state-your-postconditions.md) — the other half of the contract.
