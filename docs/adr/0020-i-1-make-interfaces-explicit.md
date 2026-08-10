# 0020. I.1: Make interfaces explicit

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

An interface is a contract between two parts of a program. Precisely stating
what is expected of a supplier of a service and a user of that service is
essential. Having good interfaces — easy to understand, encouraging efficient
use, not error-prone, supporting testing — is probably the most important single
aspect of code organisation. That is the premise of the whole I section, and
this record is its first rule.

The reason for I.1 specifically is correctness: assumptions not stated in an
interface are easily overlooked and hard to test.

Controlling the behaviour of a function through a global, namespace-scope
variable — a call mode — is implicit and potentially confusing:

```cpp
int round(double d)
{
    return (round_up) ? ceil(d) : d;    // don't: "invisible" dependency
}
```

It will not be obvious to a caller that two calls of `round(7.2)` might give
different results.

Reporting through non-local variables, `errno` for example, is just as easily
ignored:

```cpp
// don't: no test of printf's return value
fprintf(connection, "logging: %d %d %d\n", x, y, s);
```

What if the connection goes down, so that no logging output is produced? The
alternative is to throw an exception: an exception cannot be ignored.

There is an exception to the rule. Sometimes the details of a set of operations
are controlled by an environment variable — normal versus verbose output, debug
versus optimised. The use of a non-local control is potentially confusing, but
here it controls only implementation details of otherwise fixed semantics.

Two alternative formulations, both worth keeping:

- Avoid passing information across an interface through non-local or implicit
  state. Note that non-`const` member functions pass information to other member
  functions through their object's state.
- An interface should be a function or a set of functions. Functions can be
  function templates, and sets of functions can be classes or class templates.

## Decision

Make interfaces explicit. Everything a function depends on and everything it
reports comes through its parameters, its return value, or — for a member
function — its object's documented state.

- Do not let a function's behaviour depend on a namespace-scope variable.
- Do not report results or failures through non-local state.
- Do not write to namespace-scope variables from a function.
- Where behaviour genuinely varies by environment, confine it to implementation
  details — verbosity, log destination — and never let it change the semantics a
  caller relies on.

## Rationale

An implicit dependency is invisible at the call site, which is precisely where
someone has to reason about whether the call is correct. It is also the thing
that makes a function untestable: a test can supply arguments, but it can only
control global state by mutating it, which couples every test to every other.

The formulation "an interface should be a function or a set of functions" is the
useful compression. If the contract is not in the signature, there is no
contract, only a convention that holds until someone edits the other file.

## Consequences

### Positive

- A call can be judged from the signature and the arguments, without knowing
  what else has run.
- Functions become testable in isolation, because their inputs are their
  parameters.
- Failures reported through the interface — a return value that must be used, or
  an exception — cannot be silently ignored.

### Negative

- Threading a dependency through the call chain instead of reading it from a
  global means wider parameter lists, which pulls against
  [0034](0034-i-23-keep-the-number-of-function-arguments-low.md).
- Context that is genuinely ambient — a logger, a clock, an allocator — becomes
  awkward, and passing it everywhere is its own kind of noise.
- The environment-variable exception is a real hole: "implementation detail" is
  a judgement, and it is exactly the judgement people get wrong.

### Neutral

- Which mechanism reports a failure is decided elsewhere; see
  [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md).
- This rule constrains how information crosses an interface, not how the
  implementation is organised behind it.

## Enforcement

- (Simple) A function should not make control-flow decisions based on the values
  of variables declared at namespace scope.
- (Simple) A function should not write to variables declared at namespace scope.

## Related

- [0021](0021-i-2-avoid-non-const-global-variables.md) — the state that makes
  implicit interfaces possible in the first place.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — the philosophy rule this
  applies to interfaces.
- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — why `errno`-style reporting is the wrong half of this.
