# 0050. C.12: Don't make data members const or references

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

`const` and reference data members are not useful, and they make types difficult
to use by rendering them either uncopyable or partially uncopyable for subtle
reasons.

```cpp
class bad {
    const int i;    // bad
    string& s;      // bad
    // ...
};
```

The `const` and `&` data members make this class "only-sort-of-copyable": it is
copy-constructible but not copy-assignable, because neither a `const` member nor
a reference member can be reseated after construction. The compiler therefore
defines the copy assignment operator as deleted, quietly, and the type fails to
work in a container or anywhere else that assigns. Nothing in the declaration
announces this.

If a member needs to refer to something, use a pointer — raw or smart, and
`gsl::not_null` if it must not be null — instead of a reference.

## Decision

Declare data members as plain, non-`const`, non-reference objects.

- Do not declare a data member `const`. Express immutability by not providing a
  mutating operation, which leaves the type assignable.
- Do not declare a data member as `T&` or `T&&`. Where the member refers to
  something the object does not own, hold a pointer, and hold a
  `gsl::not_null<T*>` where null is not a legal state, per
  [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md).
- Where the member does own what it refers to, hold it by value or in a smart
  pointer rather than referring to it at all.
- If a type is genuinely meant not to be assignable, say so with
  `operator=(const T&) = delete` rather than achieving it as a side effect of a
  member's declaration.

## Rationale

The problem is that the restriction is invisible and misattributed. A `const`
member is written to express an intention about values, and what it actually
does is delete an operation on the enclosing type; the error surfaces far away,
at the first assignment or the first `std::sort`, in a message about the class
rather than about the member.

Deleting assignment explicitly gets the same effect with the reason attached, so
the only thing lost by following this rule is the accident. A pointer member
carries the same "refers to something else" meaning as a reference member while
remaining reseatable, which is what assignment needs.

## Consequences

### Positive

- Types stay assignable, so they can be stored, sorted, and reassigned.
- A deliberate restriction on assignment is stated deliberately, where a reader
  will find it.
- Rebinding a member becomes possible, which is what copy assignment requires.

### Negative

- A pointer member loses the compiler's guarantee of non-nullness, so a class of
  errors that references made impossible becomes possible again, and
  `gsl::not_null` is a library substitute rather than a language one.
- Immutability that was enforced by the type must now be maintained by
  discipline: nothing stops a future member function from writing to the member.
- Reference members can be initialised more cheaply and read without an
  indirection at the source level; replacing them adds `*` and `->` noise to
  every use.

### Neutral

- The rule concerns data members only. `const` on locals, parameters and member
  functions is unaffected and is encouraged by
  [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md).
- A `const` member of a type that is deliberately move-only and never assigned
  breaks nothing in practice, but the rule declines to depend on that staying
  true.

## Enforcement

- Flag any data member declared `const`, `&`, or `&&`.

## Related

- [0049](0049-c-11-make-concrete-types-regular.md) — a partially copyable type
  is exactly what regularity rules out.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — how
  to recover the guarantee a reference member gave.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the preference
  this record limits, and where it still applies.
- [0056](0056-c-32-if-a-class-has-a-raw-pointer-t-or-reference-t-consider-whether-it-might-be-owning.md)
  — the ownership question raised by the pointer member this record permits.
