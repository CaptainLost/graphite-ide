# 0311. Con.1: By default, make objects immutable

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Con (Constants and immutability), transcribed 2026-08-10

## Context

Immutable objects are easier to reason about, so make objects non-`const` only
when there is a need to change their value. Doing so prevents accidental or
hard-to-notice change of value.

The section this rule opens exists because a constant cannot be raced on, a
program with fewer moving values is easier to hold in the head, and an interface
that promises "no change" to the objects passed to it is easier to read.
[0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) states that
preference as a philosophy rule and deliberately leaves the detail to here; this
record is the part of that detail that concerns objects.

The example is a loop variable, which is where the default is most often
forgotten:

```cpp
for (const int i : c) cout << i << '\n';    // just reading: const
for (int i : c) cout << i << '\n';          // BAD: just reading
```

There is an exception. Function parameters passed by value are rarely mutated,
but also rarely declared `const`. To avoid confusion and a large number of false
positives, the rule is not enforced for function parameters:

```cpp
void f(const char* const p); // pedantic
void g(const int i) { ... }  // pedantic
```

A function parameter is a local variable, so changes to it are local, and the
`const` adds nothing a caller can observe. Note that this exception is about the
top-level `const` on a by-value parameter. Whether a pointer or reference
parameter points to `const` is a different question, and one the caller very
much can observe.

## Decision

Make immutability the default for objects, and require a reason for mutability.

- Declare a local `const` unless it is assigned after initialisation.
- Bind a range-`for` variable by `const` reference, or by `const` value for
  cheap element types, whenever the loop only reads.
- Declare namespace-scope and `static` objects `const` or `constexpr`; a mutable
  one is a separate problem, covered by
  [0152](0152-r-6-avoid-non-const-global-variables.md).
- Do not write a top-level `const` on a by-value parameter in a declaration, and
  do not treat its absence as a claim that the parameter is modified.
- When a `const` has to be removed to make an edit compile, treat that as a
  question about the edit rather than about the `const`.

## Rationale

The rule is worth having only as a default, and this is why it is stated as one.
`const` where someone happened to think of it carries no information; `const`
everywhere it is true makes its absence a signal, and a reader can then find the
mutable state of a function by scanning declarations rather than by reading
every statement in between.

The parameter exception is the interesting half. It is the one place where the
guidelines trade a true statement away for a readable one, on the grounds that a
promise nobody can depend on is not worth the width it occupies in a signature.

## Consequences

### Positive

- The mutable objects in a scope become visible at a glance, which is the set a
  reader actually has to track.
- Accidental assignment becomes a compile error rather than a behaviour change.
- Constants cannot be involved in a data race, so concurrency questions about
  them do not arise.
- Binding loop variables by `const` reference removes an incidental copy per
  iteration as a side effect.

### Negative

- `const` on a local prevents moving out of it: returning or passing a `const`
  local copies where a non-`const` one would have moved, and nothing warns.
- The exception makes the default inconsistent. `const` is claimed as the
  default for objects, then suspended for the objects that appear in signatures,
  so a reader must know the exception to interpret a declaration correctly.
- Applying the default to data members runs into
  [0050](0050-c-12-dont-make-data-members-const-or-references.md): a `const`
  member makes the whole class non-assignable, a cost paid far from the
  declaration.
- Automated enforcement cannot tell an object that was not meant to vary from
  one that merely did not vary in this version of the code, so every suggestion
  needs a human decision.

### Neutral

- The rule is a default, not a prohibition. Mutable state remains legitimate
  where the design calls for it; what changes is which case has to be justified.
- Whether member functions should be `const` is the neighbouring rule, not this
  one.
- [0314](0314-con-4-use-const-to-define-objects-with-values-that-do-not-change-after-construction.md)
  covers nearly the same ground from the other end: this record states the
  default and the parameter exception, while that one gives the criterion for
  deciding a single declaration. Neither restates the other's argument.

## Enforcement

Flag non-`const` variables that are not modified, excluding parameters, where
flagging would produce too many false positives to be useful.

## Related

- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the philosophy
  rule that states this default and defers its detail to this section.
- [0188](0188-es-25-declare-an-object-const-or-constexpr-unless-you-want-to-modify-its-value-later-on.md)
  — the same default stated at the declaration, with the reading argument in
  full.
- [0312](0312-con-2-by-default-make-member-functions-const.md) — the same default
  applied to member functions.
- [0315](0315-con-5-use-constexpr-for-values-that-can-be-computed-at-compile-time.md)
  — where an immutable value is also known during translation.
