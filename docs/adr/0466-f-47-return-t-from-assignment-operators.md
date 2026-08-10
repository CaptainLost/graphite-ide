# 0466. F.47: Return T& from assignment operators

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

The convention for operator overloads, especially on concrete types, is for
`operator=(const T&)` to perform the assignment and then return non-const
`*this`. This ensures consistency with standard-library types and follows the
principle of "do as the ints do".

```cpp
class Foo
{
 public:
    // ...
    Foo& operator=(const Foo& rhs)
    {
      // Copy members.
      // ...
      return *this;
    }
};
```

Historically there was some guidance to make the assignment operator return
`const T&`. This was primarily to avoid code of the form `(a = b) = c`. Such
code is not common enough to warrant violating consistency with the standard
types.

## Decision

Return a non-const `T&` referring to `*this` from every assignment operator.

- Give copy assignment, move assignment, and every compound assignment
  (`operator+=`, `operator|=`, and the rest) the return type `T&`, and end them
  with `return *this;`.
- Do not return `const T&`, and do not return `void`, however unlikely chained
  assignment is for the type in question.
- Do not return `T` by value; that copies for no reason and changes the meaning
  of an assignment used as a subexpression.
- Where the assignment can be defaulted, write `= default` and let the compiler
  produce the conventional signature.

## Rationale

The argument is consistency rather than utility. Chained assignment is rare and
`(a = b) = c` is rarer still, but a type that departs from the standard
signature is a type that cannot be substituted for a built-in or a
standard-library type in a template, and the failure appears at the point of
instantiation rather than at the point of the decision.

The historical `const T&` return was a design that protected against a mistake
nobody makes, at the price of making the type irregular. Regularity is worth
more, which is why the guidance changed.

## Consequences

### Positive

- The type behaves like `int` and like the standard containers under assignment,
  so generic code accepts it without special-casing.
- Chained and compound assignment work, including in the idioms the standard
  library's own algorithms use.
- The signature is uniform across every type in the codebase, so a reviewer
  never has to think about it.

### Negative

- It permits `(a = b) = c`, which is legal, useless, and now undiagnosed — the
  exact thing the `const T&` convention was invented to stop.
- Returning `*this` from a move assignment operator is easy to get wrong when
  self-assignment is possible, and the conventional signature does nothing to
  help; the correctness burden stays entirely in the body.
- It is a rule that has to be applied by hand to every hand-written assignment
  operator, and the omission compiles fine as long as nothing chains.

### Neutral

- The rule fixes the return type only. Whether the operator should exist,
  whether it should be defaulted, and how it handles self-assignment are
  separate questions.
- Prefer `= default` where possible; a defaulted operator satisfies this record
  automatically.

## Enforcement

- This should be enforced by tooling, by checking the return type and the return
  value of any assignment operator.

## Related

- [0463](0463-f-44-return-a-t-when-copy-is-undesirable-and-returning-no-object-isnt-needed.md)
  — the general case for returning a reference rather than a copy.
- [0464](0464-f-45-dont-return-a-t.md) — the reference kind that must not be
  returned, including from an operator.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a type
  whose operators match the standard ones is usable where they are.
- [0009](0009-p-3-express-intent.md) — following the conventional signature is
  how a type says it is an ordinary value.
