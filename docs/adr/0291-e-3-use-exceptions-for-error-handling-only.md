# 0291. E.3: Use exceptions for error handling only

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

The point is to keep error handling separated from "ordinary code". C++
implementations tend to be optimised on the assumption that exceptions are rare,
so using a throw as a jump costs far more than the jump it replaces.

```cpp
// don't: exception not used for error handling
int find_index(vector<string>& vec, const string& x)
{
    try {
        for (gsl::index i = 0; i < vec.size(); ++i)
            if (vec[i] == x) throw i;  // found x
    }
    catch (int i) {
        return i;
    }
    return -1;   // not found
}
```

This is more complicated and most likely runs much slower than the obvious
alternative. There is nothing exceptional about finding a value in a vector.

The same argument applies to any error-signalling channel, not only to
exceptions. A mechanism reserved for failure carries information precisely
because it is reserved: a reader who sees it knows something went wrong. A
mechanism used for both failure and ordinary flow carries no such information,
and the reader must work out which case is in front of them.

## Decision

Throw only to report an error, and use the failure channel for failure only.

- Do not throw to escape a loop, to return a value, or to unwind out of a search
  that succeeded. Where control flow is what is wanted, write control flow.
- Do not signal an outcome the caller expects and treats as normal as a failure.
  A lookup that legitimately finds nothing returns an empty result or a
  `std::optional`, not an error.
- Do not smuggle a value through an error channel by encoding it in the
  exception object.
- A `catch` clause that produces the function's ordinary answer is a sign the
  throw was not an error in the first place.

## Rationale

The value of a channel reserved for errors is diagnostic, not mechanical: it
lets a reader classify a call site without reading the callee. Every ordinary
use spent on it erodes that for every other use.

The performance argument is real but secondary. `find_index` is slower because
implementations optimise for the throw never happening, and that asymmetry is
the whole reason exceptions are cheap on the path that matters. Spending the
mechanism on ordinary control flow spends that asymmetry too: the cold path
becomes a hot one, and it was compiled on the assumption that it would not be.

## Consequences

### Positive

- A failure channel that appears only on failure is readable as such at a
  glance.
- The optimiser's assumption that the error path is cold stays true, so the
  ordinary path stays fast.
- The error/result distinction stated in
  [0289](0289-e-1-develop-an-error-handling-strategy-early-in-a-design.md) gets
  a concrete test: if the caller routinely expects it, it is not an error.

### Negative

- The line between "expected outcome" and "error" is a judgement, and reasonable
  people put it in different places for the same function. The rule does not
  decide the hard cases.
- Keeping the two apart means some functions must express both at once — an
  operation that can fail *and* can legitimately find nothing needs to say so
  twice, which is more ceremony than conflating them would be.
- Enforcement is heuristic at best. Nothing can tell an abusive throw from a
  legitimate one by inspection of types.
- Some C interfaces genuinely conflate the two, returning one code for "failed"
  and another for "nothing there", and the conflation has to be untangled by
  hand wherever such an interface is wrapped.

### Neutral

- The rule constrains the use of a mechanism, not the choice of one; it holds
  for any channel reserved for failure.
- Where the answer really is "no result and that is fine", `std::optional` is
  the shape the rule is pushing towards.

## Enforcement

- This would need to be heuristic. Look for exception values "leaked" out of
  catch clauses.

## Related

- [0290](0290-e-2-throw-an-exception-to-signal-that-a-function-cant-perform-its-assigned-task.md)
  — what a failure is; this record says not to use the channel for anything
  else.
- [0302](0302-e-17-dont-try-to-catch-every-exception-in-every-function.md) — a
  catch that produces the ordinary answer is usually also a catch that should
  not exist.
- [0009](0009-p-3-express-intent.md) — a throw used as a jump states the wrong
  intent.
