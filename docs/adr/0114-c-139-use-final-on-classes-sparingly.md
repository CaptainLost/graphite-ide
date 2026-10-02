# 0114. C.139: Use final on classes sparingly

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Capping a hierarchy with `final` classes is rarely needed for logical reasons,
and can be damaging to the extensibility of a hierarchy.

```cpp
// bad
class Widget { /* ... */ };

// nobody will ever want to improve My_widget (or so you thought)
class My_widget final : public Widget { /* ... */ };

class My_improved_widget : public My_widget { /* ... */ };  // error: can't do that
```

Not every class is meant to be a base class. Most standard-library classes are
examples of that — `std::vector` and `std::string` are not designed to be
derived from, and yet neither is marked `final`. This rule is about using
`final` on classes with virtual functions meant to be interfaces for a class
hierarchy.

Capping an individual virtual function with `final` is error-prone, because
`final` can easily be overlooked when defining or overriding a set of functions.
Fortunately the compiler catches such mistakes: a `final` member cannot be
re-declared or re-opened in a derived class.

Claims of performance improvements from `final` should be substantiated. Too
often such claims are based on conjecture, or on experience with other
languages. There are examples where `final` can be important for both logical
and performance reasons — one is a performance-critical AST hierarchy in a
compiler or language analysis tool, where new derived classes are not added
every year and only by library implementers. However, misuses are, or at
least have been, far more common.

## Decision

Do not mark a class `final` unless there is a stated reason.

- Leave a class in a hierarchy open by default; a class not intended as a base
  does not need `final` to communicate that, as the standard library
  demonstrates.
- Mark a class `final` only where extending it would be logically wrong, or
  where a measured performance benefit has been demonstrated on this code with
  this compiler.
- Do not mark a class `final` on the strength of an expected devirtualisation;
  measure first, and record the measurement.
- Where `final` is applied, expect the set of derived classes to be closed and
  under the same maintainer's control, as in a compiler's AST hierarchy.
- Use `final` on individual virtual functions sparingly too, per
  [0103](0103-c-128-virtual-functions-should-specify-exactly-one-of-virtual-override-or-final.md).

## Rationale

`final` is a decision made by the author of a class about code they have not
seen, on behalf of people they do not know. The comment in the example — "nobody
will ever want to improve `My_widget`" — is the entire argument against it: the
prediction is unfalsifiable when made and irreversible for anyone downstream who
finds it wrong.

The performance justification usually does not survive contact with measurement.
`final` does let a compiler devirtualise a call whose static type is the final
class, but that only pays where such calls are hot and were not already being
devirtualised by inlining or by a visible dynamic type; the guideline asks for
evidence on this code with this compiler, rather than the intuition imported
from languages where sealing a class does more.

## Consequences

### Positive

- Hierarchies stay extensible by people who were not consulted when they were
  designed.
- Performance claims about `final` get measured rather than assumed.
- The uncommon cases where `final` is right stand out, because they are rare and
  justified.

### Negative

- Leaving classes open means a class never intended as a base can be derived
  from anyway, and the resulting object sliced or misused; `final` would have
  prevented that at no run-time cost.
- Where devirtualisation genuinely does apply — a hot dispatch in an AST walk —
  declining `final` by default costs real performance until someone measures and
  reverses the decision.
- The rule offers no test for "sparingly", so it is a disposition rather than a
  criterion, and is not mechanically checkable beyond flagging every use.

### Neutral

- `final` on a member function has a different risk profile: it is easy to
  overlook, but the compiler catches the resulting mistake.
- On a class already declared `final`, marking individual functions `override`
  or `final` makes no difference.

## Enforcement

- Flag uses of `final` on classes.

## Related

- [0103](0103-c-128-virtual-functions-should-specify-exactly-one-of-virtual-override-or-final.md)
  — the same caution about `final` on individual functions.
- [0107](0107-c-132-dont-make-a-function-virtual-without-reason.md) — the
  opposite failure: adding polymorphism rather than closing it off.
- [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md)
  — a hierarchy that models its concept correctly is one worth leaving open.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the performance claim `final`
  is usually justified by, and which this rule asks to be measured.
