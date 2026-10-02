# 0368. T.150: Check that a class matches a concept using static_assert

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

If a class is intended to match a concept, verifying that early saves the users
of the class a great deal of pain. The alternative is that the mismatch is
discovered by whoever first instantiates a template with the class, as a
diagnostic pointing into the template rather than at the class that failed to
model what it claimed.

```cpp
class X {
public:
    X() = delete;
    X(const X&) = default;
    X(X&&) = default;
    X& operator=(const X&) = default;
    // ...
};
```

Somewhere, possibly in an implementation file, let the compiler check the
desired properties of `X`:

```cpp
static_assert(Default_constructible<X>);    // error: X has no default constructor
static_assert(Copyable<X>);                 // error: we forgot to define X's move constructor
```

Both failures are of the kind that is obvious once stated and invisible
otherwise. The deleted default constructor is deliberate-looking. The second
comment is upstream's own, and it misnames the omission: `X` does declare a move
constructor, and what it lacks is the move assignment operator, suppressed by
the user-declared copy assignment and disguised by the defaulted members around
it. That the guidelines' own worked example gets the missing member wrong is
itself the argument for letting the compiler answer the question.

## Decision

Assert the concepts a class is intended to model, at the class.

- For each concept a class claims to model, write a `static_assert` naming the
  class and the concept.
- Place the assertions where they cost nothing at every inclusion — an
  implementation file is enough, since the check is on the class, not on the
  point of use.
- Assert the ordinary properties too, not only the domain concept: default
  constructibility, copyability, movability, and comparability where they are
  part of the intent.
- Write the assertions when the class is written, so a defaulted or deleted
  special member that breaks the claim fails immediately.
- Re-run the check when the class changes: adding a member that suppresses an
  implicit special member is the usual way a class stops modelling what it did.

## Rationale

The assertion moves the error from the point of instantiation to the point of
definition, which changes both who sees it and what it says. Instead of a
template's internals failing for a user who did nothing wrong, the author gets
a one-line diagnostic naming the concept that is not satisfied. It also turns
an intention that was previously only in a comment or in nobody's head into
something the compiler rechecks on every build.

## Consequences

### Positive

- Failures are reported at the class, against the concept, rather than deep
  inside an unrelated template instantiation.
- The set of concepts a class means to model becomes documented in a form that
  cannot go stale silently.
- A special member accidentally suppressed by a later edit is caught by the
  build rather than by a user.

### Negative

- The assertion only checks the syntax a concept requires. A class can satisfy
  every expression in a concept and still not behave as the concept's semantics
  demand, so a passing check is weaker evidence than it looks.
- It is a second statement of intent that has to be maintained alongside the
  first; when a class deliberately stops modelling a concept, the assertion has
  to be found and removed, and it may not be near the class.
- Only what someone thought to assert is checked, and the omissions are exactly
  the properties nobody remembered — which are also the ones most likely to be
  missing.
- Upstream rates enforcement as not feasible: no tool can tell that a class was
  intended to model a concept for which no assertion was written.
- Assertions placed in an implementation file are easy to miss when reading the
  header, and easy to delete when the file is refactored.

### Neutral

- Where a class is used only through constrained templates, the constraints
  will report the mismatch anyway; the assertion buys an earlier and clearer
  report, not a check that was otherwise absent.
- The rule says nothing about which concepts a class ought to model.

## Enforcement

Not feasible.

## Related

- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — the
  concepts this rule checks a class against have to exist and be named first.
- [0325](0325-t-20-avoid-concepts-without-meaningful-semantics.md) — the limit
  of the check: a `static_assert` verifies syntax, never semantics.
- [0326](0326-t-21-require-a-complete-set-of-operations-for-a-concept.md) — a
  complete concept is what makes the assertion catch the missing move
  assignment rather than pass.
- [0339](0339-t-48-if-your-compiler-does-not-support-concepts-fake-them-with-enable-if.md)
  — the fallback when concepts are unavailable, which the same assertions can
  still be written against.
