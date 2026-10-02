# 0404. SL.io.2: When reading, always consider ill-formed input

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Errors are typically best handled as soon as possible. If input is not
validated, every function must be written to cope with bad data, and that is not
practical. Upstream supplies the reason and leaves the example as a placeholder.

The argument is about where the obligation lands. Input that has not been
checked at the point of reading does not become correct further in; it becomes
an assumption, and every function downstream either carries a check for it or
carries a precondition that nothing verifies. Both are worse than checking once,
at the point where the data is still recognisable as input and where a
diagnostic can name the line and column that were wrong.

Ill-formed input is also the ordinary case rather than the exceptional one. A
file the user edited by hand, a truncated read, a number where a word was
expected, a stream that reached end-of-file mid-record: none of these are
unusual, and none of them are the program's fault.

## Decision

Treat every read as capable of failing, and decide what happens before writing
the code that uses the result.

- Check the state of the stream, or the result of the read, before using the
  value it produced; a failed extraction leaves the destination unchanged or
  value-initialised, not obviously wrong.
- Validate at the boundary where input enters, and convert to types whose
  invariants hold from then on, so that downstream code has nothing left to
  check.
- Report what was wrong, with enough position information to locate it in the
  input, rather than failing silently or aborting.
- Do not let an unvalidated value become a precondition of everything
  downstream.
- Decide once, for the program, how a malformed input is reported — as a thrown
  exception, as a returned error, or as an immediate stop — and use that
  decision consistently at every reading boundary rather than per call site.

## Rationale

The sharpest clause is that handling bad data everywhere is not practical.
Validation is not merely cheaper at the boundary — it is the only place it can
be done once. Spread across the program it is done many times, inconsistently,
and the inconsistencies are where the defects live.

The corollary is that a validating boundary is what allows the rest of the code
to be written without defensive checks, which is the actual payoff.

## Consequences

### Positive

- Bad data is rejected where it can still be described to the user.
- Code past the boundary can rely on its inputs, so defensive checks disappear
  from places that had no way to recover anyway.
- Failures become reportable events rather than undefined behaviour or wrong
  results.

### Negative

- Validation code is bulk. A parser that checks everything is several times the
  size of one that assumes well-formed input, and the checking paths are the
  least-tested code in the program.
- The rule gives no criterion for what counts as validated, so it can be
  satisfied nominally by checking that a read succeeded while ignoring whether
  the value makes sense.
- Deciding what to do about bad input is a design question with no default: a
  reader can skip, substitute, truncate, or fail, and the rule does not choose.
- Reporting position information requires the reader to track it, which
  complicates otherwise simple stream code.

### Neutral

- Upstream provides no example and no enforcement for this rule, so the record
  states the principle rather than transcribing a demonstration.
- The rule applies to any input, not only to iostreams.
- It requires that ill-formed input be considered; it does not prescribe the
  mechanism by which the failure is reported.

## Related

- [0013](0013-p-7-catch-run-time-errors-early.md) — the philosophy rule this is
  the input-facing form of.
- [0289](0289-e-1-develop-an-error-handling-strategy-early-in-a-design.md) —
  what to do with the failure once it is detected.
- [0024](0024-i-5-state-preconditions-if-any.md) — an unvalidated input becomes
  an unstated precondition on everything downstream.
- [0403](0403-sl-io-1-use-character-level-input-only-when-you-have-to.md) —
  simpler reads still fail, and still have to be checked.
