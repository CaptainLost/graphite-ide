# 0055. C.31: All resources acquired by a class must be released by the class's destructor

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Prevention of resource leaks, especially in error cases. For resources
represented as classes with a complete set of default operations, this happens
automatically.

```cpp
class X {
    ifstream f;   // might own a file
    // ... no default operations defined or =deleted ...
};
```

`X`'s `ifstream` implicitly closes any file it might have open upon destruction
of its `X`.

```cpp
class X2 {     // bad
    FILE* f;   // might own a file
    // ... no default operations defined or =deleted ...
};
```

`X2` might leak a file handle. Nothing about the member says whether it owns the
file, and nothing releases it.

There is a hard case. What about a socket that will not close? A destructor, a
`close` or a cleanup operation should never fail. If one does nevertheless, the
problem has no really good solution. The writer of a destructor does not know
why the destructor is being called and cannot refuse to act by throwing. Making
it worse, many close and release operations are not retryable. Many have tried
to solve this; no general solution is known. If at all possible, treat failure
to close or clean up as a fundamental design error and terminate.

A class may hold pointers and references to objects it does not own, and those
must obviously not be deleted by its destructor:

```cpp
Preprocessor pp { /* ... */ };
Parser p { pp, /* ... */ };
Type_checker tc { p, /* ... */ };
```

Here `p` refers to `pp` but does not own it.

## Decision

Ensure that everything a class acquires is released by that class's destructor,
and that nothing else is.

- Represent each acquired resource as a member whose own destructor releases it,
  so that the class's destructor is generated and correct.
- Where no such handle type exists, write the destructor and release the
  resource there, covering every path out of the class.
- Do not release what the class does not own. A member that refers to an object
  owned elsewhere is an observer and the destructor must leave it alone.
- Say which pointers are owners — `std::unique_ptr`, or `gsl::owner<T*>` where a
  raw pointer must be kept — so that the distinction is in the code rather than
  in a comment.
- Treat a failure to release as a design error and terminate, per
  [0059](0059-c-36-a-destructor-must-not-fail.md).

## Rationale

The destructor is the only place in the language guaranteed to run on every exit
path, including the ones nobody wrote — an early return, a `throw` from a
callee, a stack unwind. Any release written anywhere else is release on the
paths the author thought of, which is why the "especially in error cases"
qualification is doing the real work in this rule.

The rule's negative half matters as much as its positive half. A destructor that
releases something the class merely observes is not a leak but a double free,
which is worse; ownership therefore has to be a property of the member's type,
not an assumption made at destruction time.

## Consequences

### Positive

- Every exit path releases, including exceptional ones nobody wrote by hand.
- Ownership becomes visible in the member declarations rather than inferred from
  the destructor body.
- Leak prevention scales: it follows from composition rather than from review.

### Negative

- The rule has a genuine hole at exactly the resources that motivate it. A
  release that can fail — an unresponsive socket, a network file handle — has no
  good answer, and the guideline's advice is to terminate, which is a serious
  cost to pay for a cleanup failure.
- Order of release is fixed by member declaration order and is not always the
  order the resources require, so an interdependent set of resources may not be
  expressible as plain members.
- Determining ownership for an existing raw pointer member requires reading the
  constructors and often the callers, which is work the rule assumes has already
  been done.

### Neutral

- Non-owning members are explicitly out of scope, and holding them is legitimate
  design.
- The rule does not say the destructor must be user-written; a generated one
  that releases through its members satisfies it.

## Enforcement

- (Simple) If a class has pointer or reference members that are owners — for
  example, marked with `gsl::owner` — they should be referenced in its
  destructor.
- (Hard) Determine whether pointer or reference members are owners when there is
  no explicit statement of ownership, for instance by examining the
  constructors.

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the principle this record
  implements at class scope.
- [0054](0054-c-30-define-a-destructor-if-a-class-needs-an-explicit-action-at-object-destruction.md)
  — when a destructor has to be written at all.
- [0056](0056-c-32-if-a-class-has-a-raw-pointer-t-or-reference-t-consider-whether-it-might-be-owning.md)
  — how to decide which members this record covers.
- [0059](0059-c-36-a-destructor-must-not-fail.md) — what happens when release
  itself fails.
