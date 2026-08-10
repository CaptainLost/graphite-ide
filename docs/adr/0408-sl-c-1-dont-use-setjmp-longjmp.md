# 0408. SL.C.1: Don't use setjmp/longjmp

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

A `longjmp` ignores destructors, thus invalidating all resource-management
strategies relying on RAII.

That single sentence is the whole of the upstream reason, and it is sufficient.
`longjmp` restores a saved execution context directly: it does not unwind, so
the automatic objects between the jump target and the point of the jump are
simply abandoned. Every one of them that held a lock, a file, an allocation or a
transaction leaks it, and the program continues from the saved point believing
those resources were released.

The failure is silent and it is not local. Nothing at the `longjmp` call names
the objects it destroys, nothing at their declarations suggests they might be
abandoned, and a function that is perfectly correct in isolation becomes a leak
the moment a caller several frames up decides to jump past it.

The mechanism also duplicates, badly, what the language already offers: a
non-local transfer of control that does run destructors is what exceptions are,
and returning a value that the caller inspects is what the alternative to them
is.

## Decision

Do not use `setjmp` and `longjmp`.

- Do not call `setjmp` or `longjmp`, and do not include a facility built on
  them.
- Express a non-local exit as a thrown exception, or as an error returned up the
  call chain and checked at each level, according to the error-handling strategy
  the program has adopted.
- Where a C library requires a `setjmp`-based error path, confine it to a
  translation unit with no objects requiring destruction between the jump target
  and the call, and convert the outcome into the program's own error mechanism
  at that boundary.

## Rationale

The prohibition is unusually clean because the conflict is total rather than
situational. RAII's entire guarantee is that a destructor runs when a scope is
left; `longjmp` leaves scopes without running them. There is no way to use both
in the same call chain and retain the guarantee, so the two are not competing
techniques to be balanced but mutually exclusive ones.

That the standard leaves the behaviour undefined when a jump would destroy
objects with non-trivial destructors only reinforces the point: the language
does not define what the combination means.

## Consequences

### Positive

- Every scope exit runs the destructors of the objects in it, so
  resource-management strategies hold everywhere.
- Non-local control flow becomes expressible in one of the two mechanisms the
  language actually defines, both of which are visible in signatures.
- A whole category of leak that no local review could detect is removed.

### Negative

- Some C libraries expose an error interface built on `setjmp` and offer no
  alternative, so the rule cannot be followed at those boundaries; it can only
  be quarantined there, and quarantining it is real work.
- The alternatives are not free: exceptions require an unwinding-capable
  environment that some constrained targets exclude, and returned errors must be
  checked and forwarded at every level.
- The rule is stated absolutely and offers no exception clause, so a legitimate
  interoperability case has to be argued as a deviation rather than as an
  application of the rule.

### Neutral

- The prohibition is on the mechanism, not on non-local control flow, which
  remains available in forms that unwind properly.
- `setjmp` alone is not useful without `longjmp`, so the rule is effectively one
  prohibition rather than two.

## Enforcement

- Flag all occurrences of `longjmp` and `setjmp`.

## Related

- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the strategy `longjmp` invalidates.
- [0294](0294-e-6-use-raii-to-prevent-leaks.md) — the same guarantee, stated
  from the error-handling side.
- [0222](0222-es-76-avoid-goto.md) — the neighbouring rule about control flow
  that jumps rather than returns.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the philosophy rule this
  protects.
