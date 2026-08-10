# 0302. E.17: Don't try to catch every exception in every function

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Catching an exception in a function that cannot take a meaningful recovery
action leads to complexity and waste. Let an exception propagate until it
reaches a function that can handle it, and let cleanup actions on the unwinding
path be handled by RAII.

```cpp
void f()   // bad
{
    try {
        // ...
    }
    catch (...) {
        // no action
        throw;   // propagate exception
    }
}
```

The `try`/`catch` here accomplishes nothing that would not have happened without
it. It is not merely redundant: it is worse than redundant, because a reader
must now check whether it does something, and because it invites a later edit to
put something in the empty handler that does not belong there.

The distinction the rule turns on is between handling an error and reacting to
it. A function that catches in order to release a resource should be using RAII
instead. A function that catches in order to log and rethrow has bought a log
line at the cost of a handler, and the same information is usually available
where the error is finally handled. A function that catches in order to add
context — the filename that was being read, say — is doing something real, and
this rule is not against that.

## Decision

Handle an error where it can be acted on, and nowhere else.

- Do not write a handler that only rethrows, and do not write one that only
  logs and rethrows.
- Use RAII for cleanup on the failure path rather than a handler; see
  [0294](0294-e-6-use-raii-to-prevent-leaks.md).
- Let an exception propagate unchanged unless the frame has something to add or
  can actually recover.
- Add context to an error only where the context is genuinely known at that
  frame and lost above it.
- Do not translate an error into a less specific one on the way past. Catching a
  precise exception type and rethrowing a generic one at an intermediate frame
  destroys the caller's ability to react.
- Nested `try` blocks and a high density of handlers are a signal that recovery
  is being attempted at the wrong level.

## Rationale

A handler is a claim that this frame knows what to do, and most frames do not.
The frames between the detection of an error and its handling have a job —
releasing what they hold — and RAII already does that job without a handler.

The clearest cost is diagnostic rather than performance. Once handlers appear at
every level, finding the one that actually decides the outcome means reading all
of them, and the error's provenance is obscured by however many layers reshaped
it on the way. Keeping the propagation path inert makes the single frame that
decides easy to find.

## Consequences

### Positive

- The frame that decides what happens is unique, and therefore findable.
- Errors reach it with their original classification intact.
- Intermediate functions stay short, because failure handling is absent from
  them rather than repeated in them.

### Negative

- An error that propagates far arrives with little context: the top-level
  handler knows the operation failed but not which of the six files it was
  reading. Adding context is exactly what this rule discourages, so the two
  goals genuinely pull against each other.
- Letting an exception travel far means every function it passes through must be
  exception-safe, including ones whose authors never considered the possibility.
  The rule removes handlers at the cost of an obligation on every frame in
  between.
- "Can take a meaningful recovery action" is not a checkable property, so the
  rule is applied by judgement and argued about in review.
- The greater the distance between where a failure is detected and where it is
  handled, the harder the connection is to follow when reading the code, and the
  less the handler knows about what it is handling.

### Neutral

- Adding context, as opposed to logging and rethrowing, is legitimate and not
  what the rule prohibits.
- A single top-level handler is compatible with the rule, and is what it
  implies: one place that decides the outcome, with inert frames beneath it.

## Enforcement

- Flag nested `try` blocks.
- Flag source files with a high ratio of `try` blocks to functions. The
  guidelines note that defining "too high" is an open problem, so this is a
  review heuristic rather than a threshold.

## Related

- [0303](0303-e-18-minimize-the-use-of-explicit-try-catch.md) — the same argument
  about the construct itself.
- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — what replaces a handler that
  existed only to clean up.
- [0289](0289-e-1-develop-an-error-handling-strategy-early-in-a-design.md) —
  deciding where errors are handled is part of the strategy.
