# 0308. E.28: Avoid error handling based on global state (e.g. errno)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Global state is hard to manage and it is easy to forget to check it. When did
you last test the return value of `printf()`?

```cpp
int last_err;

void f(int n)
{
    // ...
    p = static_cast<X*>(malloc(n * sizeof(X)));
    if (!p) last_err = -1;     // error if memory is exhausted
    // ...
}
```

C-style error handling is based on the global variable `errno`, so it is
essentially impossible to avoid this style completely.

The failures compound. A global indicator is not connected to any particular
call, so a caller must know to look, must know when to look, and must look
before anything else overwrites it — and "anything else" includes calls made by
the library, the logging, and the debugger. It is not scoped, so an error set in
one component is read by another that had no relationship to it. It does not
survive threading without special provision; `errno` is thread-local by
requirement precisely because the naive version does not work at all. And
because reading it is optional, the default behaviour of code that ignores it is
to carry on with a wrong value.

The rhetorical question about `printf` is the strongest part of the rule. Almost
nobody checks it, the interface permits that, and so a whole class of failure is
routinely discarded by omission — not by a decision anyone made.

The same prohibition appears from the interface side in
[0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md),
whose enforcement note is simply "look for `errno`", and
[0021](0021-i-2-avoid-non-const-global-variables.md) rules out the mutable
global that this style depends on.

## Decision

Tie a failure to the call that produced it. Never report one through global or
otherwise non-local state.

- Do not introduce a global, static, or thread-local error variable, and do not
  add a "last error" accessor to a class as a substitute.
- Report the failure through the call itself — by throwing, per
  [0290](0290-e-2-throw-an-exception-to-signal-that-a-function-cant-perform-its-assigned-task.md),
  or where that is impossible by returning an indicator, per
  [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md).
- Where a C API reports through `errno` or an equivalent, read it immediately
  after the call that set it — before any other call — and convert it there into
  whatever the surrounding code uses.
- Confine that reading to the wrapper around the C interface. `errno` should not
  be consulted anywhere above the layer that translates it.
- Do not let a failure be discardable by omission. If a caller may ignore an
  outcome, that is a decision the interface should force them to make
  explicitly.

## Rationale

A global error indicator inverts the default. Every other error mechanism makes
handling the path of least resistance and ignoring it the deliberate act; this
one makes ignoring it what happens when nobody does anything, which is what
happens most of the time.

The `printf` observation shows this is not a hypothetical. The mechanism has
been in universal use for decades and the near-universal practice is not to
check it — not because anyone decided the failure did not matter, but because
nothing ever asked.

## Consequences

### Positive

- A failure is attached to the call that produced it, so it cannot be attributed
  to the wrong operation or overwritten by an unrelated one.
- Ignoring a failure becomes an explicit act rather than the default.
- The mechanism works under concurrency without special provision, because
  nothing is shared.

### Negative

- It cannot be followed completely. `errno` is how the C library reports, and
  any code touching POSIX or the CRT reads it, so the style survives at every
  such boundary.
- Reading `errno` correctly is itself delicate: it must be read immediately,
  many functions set it even on success, and some set it only on failure. The
  wrapper has to know which, per function.
- Wrapping every C call to convert its error code is real code that adds nothing
  functional, and it is exactly at the boundaries where the code is least
  pleasant to write.
- A "last error" accessor is genuinely convenient for a stateful object such as
  a parser, and forbidding it means threading the error through every call that
  might have produced it.

### Neutral

- The rule targets error reporting through global state specifically, not global
  configuration or logging.
- Calling an interface that reports through `errno` does not violate the rule;
  building one does.

## Enforcement

- Awkward. Look for `errno` and for global or static variables used as error
  indicators.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the general rule against
  the state this style depends on.
- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the same prohibition stated from the interface side.
- [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md)
  — the acceptable form of error-code handling.
