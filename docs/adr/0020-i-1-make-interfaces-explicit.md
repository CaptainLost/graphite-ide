# 0020. I.1: Make interfaces explicit

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The first interface rule, and the one the rest of the section elaborates. It
bears on `Core/Platform/Window.h`, `Core/Ui/Panel.h`,
`Core/Sln/ProjectContentReader.h` and `Core/Sln/SolutionEditor.h` — the four
places in this repository where an interface is declared as such.

## Decision

Adopt I.1 as written. Transcribed in full:

> **Reason.** Correctness. Assumptions not stated in an interface are easily
> overlooked and hard to test.

**Example, bad.** Controlling the behavior of a function through a global
(namespace scope) variable (a call mode) is implicit and potentially confusing.
For example:

```cpp
int round(double d)
{
    return (round_up) ? ceil(d) : d;    // don't: "invisible" dependency
}
```

It will not be obvious to a caller that the meaning of two calls of `round(7.2)`
might give different results.

**Exception.** Sometimes we control the details of a set of operations by an
environment variable, e.g., normal vs. verbose output or debug vs. optimized.
The use of a non-local control is potentially confusing, but controls only
implementation details of otherwise fixed semantics.

**Example, bad.** Reporting through non-local variables (e.g., `errno`) is
easily ignored. For example:

```cpp
// don't: no test of fprintf's return value
fprintf(connection, "logging: %d %d %d\n", x, y, s);
```

What if the connection goes down so that no logging output is produced?

Alternative: Throw an exception. An exception cannot be ignored.

Alternative formulation: Avoid passing information across an interface through
non-local or implicit state. Note that non-`const` member functions pass
information to other member functions through their object's state.

Alternative formulation: An interface should be a function or a set of
functions. Functions can be function templates and sets of functions can be
classes or class templates.

**Enforcement.**

- (Simple) A function should not make control-flow decisions based on the values
  of variables declared at namespace scope.
- (Simple) A function should not write to variables declared at namespace scope.

## Rationale

Adopted as written, with one substitution. The rule's "Alternative: Throw an
exception. An exception cannot be ignored" is not available here — this project
returns `Result<T>` by
[0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md).
The property the rule is after is that the failure cannot be silently dropped,
and `[[nodiscard]]` on `Result<T>` delivers exactly that, enforced by
warnings-as-errors. Read the alternative as "return a `Result<T>`".

Both enforcement points are checkable by eye and this codebase very nearly
passes them: the only namespace-scope mutable state is two font pointers in
`ImGuiRenderer.cpp`, recorded under
[0021](0021-i-2-avoid-non-const-global-variables.md).

## Consequences

### Positive

- Names the property the whole `Result<T>` scheme exists to provide: an outcome
  a caller cannot fail to see.
- The two enforcement points give a concrete review question about hidden state.

### Negative

- `Application::Get()` and `Logger::Get()` are implicit state reachable from
  anywhere, which is precisely the "invisible dependency" the rule opens with —
  see [0022](0022-i-3-avoid-singletons.md).

### Neutral

- The `errno` example has no subject here; the codebase does not read `errno`,
  and Win32 error reporting goes through `GetLastError()` at the call, wrapped
  into an `Error` immediately.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the next rule.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  what stands in for "throw an exception" here.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — P.1, of which this is the
  interface-shaped case.
