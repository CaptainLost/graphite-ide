# 0296. E.8: State your postconditions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

The reason given is short: to avoid interface errors. The rule is repeated in
the error-handling section because a postcondition is the definition of what it
means for the function to have succeeded. Without one, "the function failed" has
no content, and the error-handling strategy has nothing to be a strategy about.

That connection is exactly how upstream defines an error elsewhere in these
rules: a function cannot achieve its advertised purpose, *including establishing
its postconditions*. A function whose postconditions are unstated has an
unadvertised purpose, so whether a given outcome counts as failure becomes a
matter of opinion — usually the opinion of whoever writes the call site, who was
not there when the function was written.

Postconditions also govern the state of things after a failure, which is the
half most often skipped. If an operation modifies an object and then fails,
somebody has to know what is left behind: the original value, a valid but
unspecified value, or nothing usable. That is a postcondition of the failure
path, and it belongs on the declaration alongside the postcondition of the
success path. Where it is not stated, callers guess, and the guess is usually
optimistic.

The concrete transcription of how to state a postcondition — as `Ensures` in the
definition, or as precise English where that is impractical, and why relying on
a comment alone is weak — is recorded at
[0026](0026-i-7-state-postconditions.md) and
[0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md).

## Decision

State what each function guarantees on success, and what it leaves behind on
failure.

- State the success postcondition on the declaration, formally where that is
  practical and in precise language otherwise.
- State what is true of every output and every modified object when the function
  reports failure. "The output is unchanged" and "the output is valid but
  unspecified" are different guarantees and callers depend on which is meant.
- Where a function can leave an object in neither of those states, change the
  function rather than documenting the hazard.
- Do not describe the algorithm in place of the guarantee. A postcondition
  constrains the result, not the route to it.
- Treat a violated postcondition as a bug in the function, not as something to
  be reported to a caller as a failure. A function that did not do what it
  promised while claiming success is broken, and no caller can act on that.

## Rationale

Failure is defined relative to a postcondition, so an error-handling strategy
built on undefined success criteria is built on nothing. This is why the rule
sits in the error-handling section at all rather than only among the interface
rules.

The failure-path postcondition is the part worth insisting on. A caller that
catches an exception, or otherwise learns that an operation failed, routinely
needs to know whether the destination object was touched before the failure, and
there is no default answer it can assume — which makes an unstated one a coin
flip performed at every call site.

## Consequences

### Positive

- "Failure" acquires a definition, so the error channel means something
  specific.
- Callers know whether they may reuse an object after a failed operation.
- The guarantee is stated once, on the declaration, instead of being
  rediscovered by reading the implementation at each call site.

### Negative

- Failure-path postconditions constrain the implementation, sometimes
  expensively: promising that an argument is unchanged on failure can force a
  build-then-commit structure with an extra allocation.
- Stated postconditions are largely unchecked. `Ensures` is conventionally
  compiled out in release builds like any assertion, and prose is checked by
  nobody.
- They duplicate information that is already in the code, so they can disagree
  with it, and a wrong postcondition is worse than none.
- Some functions have postconditions that cannot be stated compactly — a
  sorted-and-stable-and-in-place guarantee — and the honest statement is long.
- The guidelines give E.8 one sentence of Reason — "to avoid interface errors" —
  and a pointer to the interface-section rule. The failure-path argument here is
  reasoned from that sentence rather than transcribed, so it cannot be checked
  against upstream argument; the transcribed substance is at
  [0026](0026-i-7-state-postconditions.md).

### Neutral

- The rule does not require a formal notation; see
  [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) for the
  preference.
- A postcondition that the return type already expresses need not be restated.

## Related

- [0026](0026-i-7-state-postconditions.md) — the full transcription, with the
  overflow and `memset` examples.
- [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) — how to state
  one formally.
- [0295](0295-e-7-state-your-preconditions.md) — the caller's half of the
  contract.
- [0292](0292-e-4-design-your-error-handling-strategy-around-invariants.md) — the
  state left behind on failure is an invariant question.
