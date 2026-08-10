# 0121. C.150: Use make_unique() to construct objects owned by unique_ptrs

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Section C states this rule as a cross-reference: the substance lives in the
resource-management rule R.23, "Use `make_unique()` to make `unique_ptr`s", and
is transcribed here.

`make_unique` gives a more concise statement of the construction, and it ensures
exception safety in complex expressions.

```cpp
unique_ptr<Foo> p {new Foo{7}};    // OK: but repetitive
auto q = make_unique<Foo>(7);      // Better: no repetition of Foo
```

The repetition is the visible cost: the type is written twice, and the two
spellings can be edited apart, leaving a `unique_ptr<Base>` constructed from a
`new Derived` that nobody intended. The exception-safety point is the one that
does not show in the example, and it has to be stated carefully. Before C++17 a
compiler could interleave the evaluation of a call's arguments, so in
`f(std::unique_ptr<A>{new A}, g())` the allocation could be performed, `g()`
could then throw, and the raw pointer would be left with no owner. C++17
requires each argument's initialisation to be sequenced as a whole against the
others, so that particular leak is closed for a codebase on C++17 or later.
What `make_unique` still
buys is that the allocation and the acquisition of ownership are one
indivisible call: there is never a raw pointer value that a later edit can
strand between the two.

## Decision

Construct an object that a `unique_ptr` will own with `std::make_unique`.

- Write `auto p = std::make_unique<T>(args...)` rather than
  `std::unique_ptr<T> p{new T{args...}}`.
- Never pass a bare `new` expression as an argument to a function, whether or
  not a smart pointer is constructed from it.
- Use `std::make_unique<T[]>(n)` for an array rather than `new T[n]`.
- Use the `unique_ptr` constructor directly only where `make_unique` cannot
  express the construction: a custom deleter, a pointer obtained from a foreign
  allocator or C API, or a type whose constructor is not accessible to
  `make_unique`. Keep such a construction adjacent to the acquisition.

## Rationale

Concision is the reason people notice and the smaller one. The argument that
decides it is that a raw `new` written in an expression produces a value owned
by nothing that some other part of that expression has to adopt, so whether it
is safe depends on the surrounding expression's shape — a question C++17's
sequencing rules answered for calls but which still has to be re-asked every
time the code is edited. `make_unique` removes the unowned value rather than
arranging for it to be briefly safe, which is the difference between an
ownership rule and a habit.

## Consequences

### Positive

- The type appears once, so the owner and the object cannot drift apart under
  editing.
- The allocation and the acquisition of ownership are a single call, so no edit
  can separate them and leave a raw pointer stranded between the two.
- `new` becomes rare enough in the source that each remaining occurrence is
  worth reading.

### Negative

- `make_unique` cannot express a custom deleter, so exactly the resources whose
  release is unusual — the ones most worth protecting — fall outside the rule.
- It value-initialises, which for large arrays or trivially constructible types
  is work the caller may not want; C++20's `std::make_unique_for_overwrite` is
  the way out, so following the rule means knowing a second factory name that
  the rule itself does not mention.
- It requires a constructor accessible from the standard library, so a type with
  a private constructor and a factory needs either a passkey or a direct
  `unique_ptr` construction.
- `auto p = make_unique<T>(...)` hides the pointer type at the declaration,
  which costs the reader something the explicit spelling gave for free.

### Neutral

- Aggregate initialisation through `make_unique` requires C++20; under earlier
  standards a type with no matching constructor cannot be created by it at all.
- This rule concerns construction only; the decision to own through a
  `unique_ptr` at all is
  [0120](0120-c-149-use-unique-ptr-or-shared-ptr-to-avoid-forgetting-to-delete-objects-created-using-new.md).

## Enforcement

- (Simple) Warn if a `unique_ptr` is constructed from the result of `new` rather
  than `make_unique`.

## Related

- [0120](0120-c-149-use-unique-ptr-or-shared-ptr-to-avoid-forgetting-to-delete-objects-created-using-new.md)
  — the rule that puts the object in a `unique_ptr` to begin with.
- [0122](0122-c-151-use-make-shared-to-construct-objects-owned-by-shared-ptrs.md)
  — the same rule for shared ownership, with an additional performance reason.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the leak this closes is the
  one the guidelines care most about.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — using the standard
  factory rather than hand-rolling the acquisition.
