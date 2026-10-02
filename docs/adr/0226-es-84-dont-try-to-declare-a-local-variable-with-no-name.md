# 0226. ES.84: Don't try to declare a local variable with no name

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

There is no such thing. What looks to a human like a variable without a name is
to the compiler a statement consisting of a temporary that immediately goes out
of scope.

```cpp
void f()
{
    lock<mutex>{mx};   // Bad
    // ...
}
```

This declares an unnamed lock object that immediately goes out of scope at the
point of the semicolon. It is not an uncommon mistake, and this particular
example can lead to hard-to-find race conditions: the mutex is locked and
unlocked on the same line, and the code that was meant to be protected runs
unguarded.

Unnamed function arguments are fine; the rule is about local declarations.

## Decision

Give every scoped object a name.

- Name every guard, lock and scope-bound object: `std::scoped_lock lock{mx};`,
  not `std::scoped_lock{mx};`.
- Treat an expression-statement that consists only of a constructor call as a
  mistake, and either name the result or delete the statement.
- Do not omit the name in the belief that it declares an anonymous variable
  whose lifetime runs to the end of the block; it does not.
- Name the object even where the name is never used again; that is what the
  object is for.

## Rationale

The whole cost of this rule is a few characters, and the whole benefit is that
the object exists for the duration you intended rather than for the duration of
one statement. The failure is silent by construction: the constructor and
destructor both run, no diagnostic fires, and with a lock the resulting race
appears only under contention.

The deeper point is that RAII types encode their contract in their lifetime, so
a lifetime typo is a contract violation with no syntactic trace. Naming the
object is what pins the lifetime to the scope.

## Consequences

### Positive

- Scope guards actually guard their scope.
- Removes a defect class that testing rarely reproduces, because the window is
  one statement wide and timing-dependent.
- A named guard can be inspected, reset or moved later if the code grows.

### Negative

- Produces variables that are declared and never mentioned again, which some
  compilers and analysers warn about as unused, so the fix for one warning
  invites another.
- Every such name is a naming decision in a place where the name carries no
  information, and repeated `lock`, `guard`, `g` names in nested scopes shade
  into shadowing, which
  [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) discourages.
- The rule does not help with the reverse error, an object named and kept alive
  far longer than intended.

### Neutral

- Unnamed function parameters are explicitly permitted and are governed by
  [0446](0446-f-9-unused-parameters-should-be-unnamed.md).
- A deliberately discarded temporary — a constructor called purely for its side
  effects — is legal; it is simply not a variable declaration, and should be
  written so that a reader can tell.

## Enforcement

Flag statements that are just a temporary.

## Related

- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the RAII objects whose lifetime this mistake destroys.
- [0446](0446-f-9-unused-parameters-should-be-unnamed.md) — the case where
  leaving the name off is the right answer.
- [0175](0175-es-5-keep-scopes-small.md) — the scope a guard is meant to span.
- [0183](0183-es-20-always-initialize-an-object.md) — the neighbouring rule on
  declarations meaning what they appear to mean.
