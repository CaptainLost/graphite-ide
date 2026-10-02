# 0029. I.10: Use exceptions to signal a failure to perform a required task

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

It should not be possible to ignore an error, because that could leave the
system or a computation in an undefined or unexpected state. This is a major
source of errors.

```cpp
int printf(const char* ...);    // bad: return negative number if output fails

template<class F, class ...Args>
// good: throw system_error if unable to start the new thread
explicit thread(F&& f, Args&&... args);
```

What is an error? An error means that the function cannot achieve its advertised
purpose, including establishing its postconditions. Calling code that ignores an
error could lead to wrong results or an undefined system state. Not being able
to connect to a remote server is not by itself an error — a server can refuse a
connection for all kinds of reasons, so the natural thing is to return a result
the caller should always check. However, if failing to make a connection is
considered an error, then a failure should throw an exception.

There is an exception to the rule. Many traditional interface functions, such as
UNIX signal handlers, use error codes like `errno` to report what are really
status codes rather than errors. There is no good alternative to using such
interfaces, so calling them does not violate the rule.

Where exceptions cannot be used — because the code is full of old-style
raw-pointer use, or because there are hard real-time constraints — the
alternative is a style that returns a pair of values:

```cpp
int val;
int error_code;
tie(val, error_code) = do_something();
if (error_code) {
    // ... handle the error or exit ...
}
// ... use val ...
```

That style unfortunately leads to uninitialised variables. Since C++17,
structured bindings can initialise the variables directly from the return value:

```cpp
auto [val, error_code] = do_something();
if (error_code) {
    // ... handle the error or exit ...
}
// ... use val ...
```

Performance is not considered a valid reason not to use exceptions. Explicit
error checking and handling often consume as much time and space as exception
handling; cleaner code often yields better performance with exceptions, by
simplifying the tracing of paths through the program and their optimisation; a
good rule for performance-critical code is to move checking outside the critical
part; more regular code gets better optimised in the longer term; and
performance claims should always be measured carefully before they are made.

## Decision

Signal a failure to perform a required task by throwing an exception.

- Distinguish an error from a status. A condition the environment routinely
  produces, and which the caller is expected to inspect and act on, is a result
  to be returned. A failure to achieve the function's advertised purpose,
  including its postconditions, is an error.
- Do not report a failure by setting non-local state such as `errno`, and do not
  report one through a return value a caller can discard in silence.
- Calling a traditional C interface that reports through an error code does not
  violate this record; such interfaces have no better alternative.
- Where exceptions genuinely cannot be used, return the value and the error
  together and consume them with structured bindings, rather than leaving
  variables uninitialised.
- Do not decline exceptions on performance grounds without a measurement.

## Rationale

The property the rule is really after is unignorability. What makes `printf`'s
interface poor is not the absence of a `throw` but that its failure is
expressible as a value a caller can drop without the compiler saying anything,
and the great majority of callers do exactly that.

An exception cannot be dropped by omission. That is a strong guarantee, and it
is bought at the price of a control-flow path that is invisible in the
signature — which is precisely the trade the rest of this record's consequences
are about.

## Consequences

### Positive

- A failure cannot be ignored by writing nothing, which is the most common way
  errors are ignored.
- The success path stays free of error plumbing, so it reads as what the
  function does.
- The error object can carry arbitrary detail rather than being squeezed into an
  integer code.

### Negative

- An exception is invisible in a function's signature, so a reader cannot tell
  from a declaration what may propagate through it or what a caller must be
  prepared for.
- Exceptions cannot cross a C ABI, and they are unavailable or forbidden in
  hard real-time and some embedded environments, so the rule has whole domains
  where it simply does not apply — as the guidelines themselves concede.
- Every intervening function must be exception-safe whether or not it knows an
  exception can pass through it, which is a discipline that costs more than the
  throw site does.
- Distinguishing an error from a status is a judgement made per interface, and
  the same condition — a refused connection — is genuinely one or the other
  depending on what the function promised.

### Neutral

- The alternative styles are recorded here as alternatives, not as
  prohibitions; the guidelines offer them for the environments where exceptions
  are unavailable.
- Precondition and postcondition violations are a different category from
  errors, and are reported differently.

## Enforcement

- (Not enforceable) This is a philosophical guideline that is infeasible to
  check directly.
- Look for `errno`.

## Related

- [0020](0020-i-1-make-interfaces-explicit.md) — reporting through non-local
  state is rejected by both rules.
- [0024](0024-i-5-state-preconditions-if-any.md) and
  [0026](0026-i-7-state-postconditions.md) — precondition and postcondition
  violations, which are not errors in this sense.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  an exception propagating past a raw owning pointer is how the leak happens.
