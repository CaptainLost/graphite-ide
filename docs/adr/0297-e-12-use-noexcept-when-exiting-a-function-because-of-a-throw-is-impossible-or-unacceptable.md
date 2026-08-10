# 0297. E.12: Use noexcept when exiting a function because of a throw is impossible or unacceptable

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

`noexcept` makes error handling systematic, robust, and efficient.

```cpp
double compute(double d) noexcept
{
    return log(sqrt(d <= 0 ? 1 : d));
}
```

Here we know that `compute` will not throw, because it is composed out of
operations that do not throw. Declaring `compute` to be `noexcept` gives the
compiler and human readers information that makes it easier for them to
understand and manipulate it. Many standard-library functions are `noexcept`,
including all the standard-library functions inherited from the C standard
library.

The second case is the interesting one, because it is a decision rather than an
observation:

```cpp
vector<double> munge(const vector<double>& v) noexcept
{
    vector<double> v2(v.size());
    // ... do something ...
}
```

The `noexcept` here states that the author is not willing or able to handle the
situation where the local `vector` cannot be constructed. That is, memory
exhaustion is considered a serious design error on a par with hardware failure,
and the author is willing to crash the program if it happens. `noexcept` is
therefore not only a claim about what the code does — it is also a declaration
about what the author refuses to treat as a recoverable condition. Violating it
calls `std::terminate`, which is the point rather than an accident.

The rule is not an instruction to decorate everything. It says to use `noexcept`
where a throw is impossible *or* where it is unacceptable, and both halves
require a judgement about the function.

Do not use traditional exception specifications; see
[0309](0309-e-30-dont-use-exception-specifications.md).

`noexcept` is load-bearing beyond documentation, because the standard library
inspects it. Reallocating a `std::vector` moves its elements only when their
move constructor is `noexcept`, and copies them otherwise, so a missing
`noexcept` on a move constructor silently costs copies where moves were
expected.

## Decision

Mark a function `noexcept` when no exception can escape it, or when one escaping
should end the program.

- Mark a function `noexcept` when it is composed of non-throwing operations, as
  `compute` above is.
- Mark a function `noexcept` when a failure it could encounter is one the design
  will not attempt to recover from, and say so where the reason is not obvious.
- Mark move constructors, move assignment, `swap`, and destructors `noexcept`;
  the standard library's behaviour depends on it.
- Do not mark a function `noexcept` merely because it does not throw today, if
  its contract admits an implementation that might. `noexcept` is part of the
  interface and removing it later breaks callers.
- Do not use a dynamic exception specification. Use `noexcept`, or nothing.
- Note that `noexcept` is a promise about escape, not about the absence of
  failure: a function that reports failure by its return value and cannot throw
  is `noexcept` and still fallible.

## Rationale

`noexcept` is the only part of a function's failure behaviour the type system
can see. Everything else in this section is convention; this one is checked,
propagates through templates, and changes which algorithms the standard library
selects.

The `munge` example is the sharper argument. `noexcept` there is a policy
statement — memory exhaustion is not recoverable in this design — and stating it
in the signature is better than the alternative, which is a `catch` block that
pretends to recover and cannot. Terminating deliberately at the point of failure
is more honest than unwinding to a handler that has nothing to do.

## Consequences

### Positive

- Move operations and `swap` marked `noexcept` let containers move rather than
  copy on reallocation, which is a real and easily missed performance
  difference.
- Readers learn from the signature which functions can fail out from under them.
- A function that must not fail is stated as such, and the statement is enforced
  at run time by `std::terminate`.

### Negative

- It is a hard commitment: `noexcept` is part of the interface, so removing it
  later is a breaking change for every caller that depended on it, including
  standard-library machinery.
- Getting it wrong terminates the process. There is no compile-time diagnostic,
  the stack is not necessarily unwound before termination, and there is no
  opportunity to log unless a terminate handler was installed to do so.
- Deciding a function is `noexcept` requires knowing that everything it calls is
  too, which is not locally verifiable and can become false when a dependency
  changes.
- It fits badly on functions that allocate, which is much of the code that would
  otherwise qualify. Marking such a function is only honest where allocation
  failure is deliberately treated as fatal, as in `munge` above.

### Neutral

- Most destructors are implicitly `noexcept` already, so marking those is
  documentation rather than a change in behaviour. The exception is a destructor
  whose base or member destructors are themselves potentially throwing.
- The rule asks for a judgement, not for blanket annotation: "impossible" and
  "unacceptable" are different findings about a function, and both licence the
  same mark.

## Related

- [0301](0301-e-16-destructors-deallocation-swap-and-exception-type-copy-move-construction-must-never-fail.md)
  — the operations that must be `noexcept`, and why.
- [0309](0309-e-30-dont-use-exception-specifications.md) — the mechanism
  `noexcept` replaced.
- [0306](0306-e-26-if-you-cant-throw-exceptions-consider-failing-fast.md) — the
  `munge` position, generalised.
