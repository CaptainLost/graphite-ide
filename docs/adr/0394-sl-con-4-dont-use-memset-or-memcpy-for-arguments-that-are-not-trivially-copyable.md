# 0394. SL.con.4: Don't use memset or memcpy for arguments that are not trivially-copyable

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Doing so messes up the semantics of the objects, for example by overwriting a
vptr. The same applies to `(w)memset`, `(w)memcpy`, `(w)memmove` and
`(w)memcmp`.

```cpp
struct base {
    virtual void update() = 0;
};

struct derived : public base {
    void update() override {}
};

void f(derived& a, derived& b) // goodbye v-tables
{
    memset(&a, 0, sizeof(derived));
    memcpy(&a, &b, sizeof(derived));
    memcmp(&a, &b, sizeof(derived));
}
```

Instead, define proper default initialisation, copy, and comparison functions:

```cpp
void g(derived& a, derived& b)
{
    a = {};    // default initialize
    b = a;     // copy
    if (a == b) do_something(a, b);
}
```

The `memcmp` case is the quietest of the three. It does not corrupt anything; it
simply answers the wrong question, comparing padding bytes and vptrs rather than
values, so two equal objects can compare unequal and stay that way for years.

## Decision

Restrict the `mem*` family to types for which byte-wise copying is the whole of
the semantics.

- Do not `memset`, `memcpy`, `memmove` or `memcmp` an object whose type is not
  trivially copyable, and do not do it to a type that owns a resource, has
  virtual functions, has a user-provided copy or move operation, or has an
  invariant.
- Assign `{}` to default-initialise, assign to copy, and compare with
  `operator==`; give the type those operations if it lacks them.
- The same prohibition covers the wide-character variants of these functions.
- Where a type is genuinely a bag of bytes and the byte operation is wanted,
  say so with `static_assert(std::is_trivially_copyable_v<T>)` at the call site.

## Rationale

The bytes of an object are not the object. A vptr, a pointer to owned storage,
a sentinel used by an invariant and a padding byte all live in that byte range,
and the `mem*` functions treat them as data. Zeroing a polymorphic object does
not clear it — it destroys the ability to call a virtual function on it — and
the crash arrives at an unrelated call site later.

This is the case where the type system has already stated the answer:
`std::is_trivially_copyable` exists precisely to say whether the byte-wise
operation is legitimate, and the rule is little more than an instruction to
consult it.

## Consequences

### Positive

- Objects keep their vptrs, their ownership, and their invariants.
- Copies go through the copy operation, so a type that acquires an owning member
  later does not silently start double-freeing.
- Comparison answers the question about values rather than about bytes, so
  padding stops affecting results.

### Negative

- A generic routine that legitimately bulk-copies must now be constrained, which
  means writing a trait check or a concept where a call previously sufficed.
- Element-wise assignment is not always compiled into the same code as a bulk
  copy, so there are narrow cases where following this rule is measurably
  slower.
- The rule does not help with the other common `mem*` defect, which is the
  length argument; a trivially-copyable type passed with the wrong size is still
  wrong and is out of scope here.
- Third-party and platform APIs that hand back structures to be zeroed by
  `memset` are common, and this rule offers no guidance for that boundary.

### Neutral

- Trivially-copyable types remain legitimate arguments to these functions; the
  rule narrows the domain, it does not ban the functions.
- Whether a type is trivially copyable is a property the compiler already
  computes, so the distinction requires no judgement.

## Enforcement

- Flag the use of these functions for types that are not trivially copyable.

## Related

- [0091](0091-c-90-rely-on-constructors-and-assignment-operators-not-memset-and-memcpy.md)
  — the same prohibition stated from the class-design side.
- [0393](0393-sl-con-3-avoid-bounds-errors.md) — the length argument to the same
  family of functions is the other way they go wrong.
- [0390](0390-sl-4-use-the-standard-library-in-a-type-safe-manner.md) — the
  umbrella rule; this is one of its concrete forms.
- [0138](0138-c-183-dont-use-a-union-for-type-punning.md) — the other common way
  an object's bytes are reinterpreted as something they are not.
