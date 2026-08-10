# 0290. E.2: Throw an exception to signal that a function can't perform its assigned task

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section E (Error handling), transcribed 2026-08-10

## Context

Throwing makes error handling systematic, robust, and non-repetitive.

```cpp
struct Foo {
    vector<Thing> v;
    File_handle f;
    string s;
};

void use()
{
    Foo bar { {Thing{1}, Thing{2}, Thing{monkey} }, {"my_file", "r"}, "Here we go!"};
    // ...
}
```

Here the `vector` and `string` constructors might not be able to allocate
sufficient memory for their elements, the `vector` constructor might not be able
to copy the `Thing`s in its initialiser list, and `File_handle` might not be
able to open the required file. In each case they throw an exception for
`use()`'s caller to handle. If `use()` could handle the failure to construct
`bar` it can take control using `try`/`catch`. In either case `Foo`'s
constructor correctly destroys the members that were constructed before passing
control to whatever tried to create a `Foo`. Note that there is no return value
that could contain an error code.

The `File_handle` constructor might be defined like this:

```cpp
File_handle::File_handle(const string& name, const string& mode)
    : f{fopen(name.c_str(), mode.c_str())}
{
    if (!f)
        throw runtime_error{"File_handle: could not open " + name + " as " + mode};
}
```

It is often said that exceptions are meant to signal exceptional events and
failures, but that is a bit circular, because "what is exceptional?" The
guidelines answer by example: a precondition that cannot be met, a constructor
that cannot construct an object and so fails to establish its class's invariant,
an out-of-range error such as `v[v.size()] = 7`, and an inability to acquire a
resource such as the network being down. In contrast, termination of an ordinary
loop is not exceptional; unless the loop was meant to be infinite, termination
is normal and expected. Do not use a throw as simply an alternative way of
returning a value from a function.

The stated exception is hard real time. Some systems require a guarantee that an
action is taken in a typically short constant maximum time known before
execution starts, and such systems can use exceptions only if there is tool
support for accurately predicting the maximum time to recover from a throw.
Before deciding that exception-based error handling cannot be afforded, look at
the alternatives; they have their own complexities and problems. And as far as
possible, measure before making claims about efficiency.

## Decision

Throw an exception to signal that a function cannot perform its assigned task.

- A function that cannot achieve its advertised purpose reports that by
  throwing, not by returning a value the caller may discard and not by setting
  non-local state.
- Classify failures the way the rule does: an unmet precondition, a constructor
  that cannot establish its class's invariant, an out-of-range access, and an
  inability to acquire a resource are failures. The ordinary termination of a
  loop, or a lookup that finds nothing where finding nothing is expected, is
  not.
- Never use a throw as an alternative way of returning an ordinary value.
- Let the constructors of members do the throwing where they can, and rely on
  the enclosing constructor to destroy the members it has already built.
- Where a hard real-time deadline applies, use exceptions only if there is tool
  support for accurately predicting the maximum time to recover from a throw;
  where there is not, see
  [0305](0305-e-25-if-you-cant-throw-exceptions-simulate-raii-for-resource-management.md)
  and [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md).
- Do not decline exceptions on efficiency grounds without a measurement.

## Rationale

The `Foo` example is the rule's own argument, and it is about composition rather
than about any single failure. Three members have three independent ways to
fail, and the correct partial teardown — destroy exactly the members already
constructed, in reverse order — is written by nobody and is nevertheless
correct. There is no return value in a constructor that could have carried the
failure instead.

The second argument is about being ignorable. A returned code is a value, and a
value can be dropped by writing nothing; that is not an act anyone has to
perform, so it is what happens by default. An exception cannot be dropped by
omission. What it costs is a control-flow path that no signature shows, which is
what the consequences below are about.

## Consequences

### Positive

- A failure cannot be ignored by writing nothing at the call site.
- Constructors gain a way to report failure at all, and partial construction is
  unwound correctly without anyone writing the unwinding.
- The success path stays free of error plumbing, so a function reads as the work
  it does rather than as a sequence of checks.
- The error object can carry arbitrary detail, rather than being compressed into
  an integer that fits alongside the legitimate return values.

### Negative

- An exception is invisible in a signature. Nothing in a declaration says what
  may propagate out of a call, so a reader cannot tell from the interface what a
  caller must be prepared for.
- Exceptions cannot cross a C ABI, so any interface exposed as C — a plugin
  boundary, a shared-library entry point, a callback handed to a C library —
  must catch everything at the boundary and convert.
- They are unavailable or forbidden in hard real-time systems and on targets too
  small to carry the runtime support, which the rule concedes as its stated
  exception.
- Every intervening function must be exception-safe whether or not its author
  knew an exception could pass through it. That discipline is spread across the
  whole call chain and costs far more than the throw site does.
- `noexcept` converts a failure into termination: a throw that reaches a
  `noexcept` boundary calls `std::terminate` rather than propagating, so
  declaring a function `noexcept` and later giving it a fallible call turns a
  recoverable failure into a crash.
- Distinguishing an error from a status is a judgement made per interface. The
  same condition — a refused connection — is genuinely one or the other
  depending on what the function promised.

### Neutral

- The rule constrains what a failure is and how it travels; it does not say what
  a caller should do on receiving one.
- Termination of a loop, an empty search result, and any other outcome the
  design expects are not failures, so this rule has nothing to say about them.
- The alternatives are described rather than forbidden, and the section devotes
  three rules to them.

## Related

- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the interface-section statement of the same rule.
- [0293](0293-e-5-let-a-constructor-establish-an-invariant-and-throw-if-it-cannot.md)
  — the constructor case, which is this rule's sharpest instance.
- [0291](0291-e-3-use-exceptions-for-error-handling-only.md) — the companion
  restriction on what may be thrown for.
- [0307](0307-e-27-if-you-cant-throw-exceptions-use-error-codes-systematically.md)
  — what to do where the stated exception applies.
