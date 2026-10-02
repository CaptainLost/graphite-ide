# 0091. C.90: Rely on constructors and assignment operators, not memset and memcpy

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The standard C++ mechanism to construct an instance of a type is to call its
constructor, and a constructor should create a fully initialised object. No
additional initialisation, such as by `memcpy`, should be required. A type
provides a copy constructor and a copy assignment operator to make a copy of
itself appropriately, preserving the type's invariants. Using `memcpy` to copy a
non-trivially-copyable type has undefined behaviour, and frequently the
result is slicing or data corruption.

```cpp
// good
struct base {
    virtual void update() = 0;
    std::shared_ptr<int> sp;
};

struct derived : public base {
    void update() override {}
};
```

```cpp
// bad
void init(derived& a)
{
    memset(&a, 0, sizeof(derived));
}
```

This is type-unsafe and overwrites the vtable.

```cpp
// bad
void copy(derived& a, derived& b)
{
    memcpy(&a, &b, sizeof(derived));
}
```

This is also type-unsafe and overwrites the vtable. In the copy case the
`shared_ptr` member is a second casualty: its control block reference count is
copied bitwise rather than incremented, so the two objects now disagree with the
allocator about how many owners exist.

## Decision

Initialise and copy objects through their own special member functions.

- Construct with a constructor; do not zero-fill an object with `memset` and
  call it initialised.
- Copy with the copy constructor or copy assignment operator, and move with the
  move equivalents; do not `memcpy` an object of class type.
- Restrict `memset`, `memcpy` and `memmove` to raw byte buffers — `std::byte`,
  `char`, or a trivially copyable aggregate with no invariant.
- Where a byte-level operation is genuinely required, assert the type is
  trivially copyable at the call site with `static_assert(
  std::is_trivially_copyable_v<T>)`.
- Give a type a member initialiser or a constructor that leaves it valid, so
  that no caller is tempted to blank it out first.

## Rationale

A class type is not a bag of bytes; it is a bag of bytes plus a set of promises
about them. `memcpy` copies the bytes and silently discards the promises — the
vtable pointer, the reference count, the invariant relating two members — which
is exactly the information the copy constructor exists to preserve.

The failure mode is what makes this worth a record. A bitwise copy of a
polymorphic object usually appears to work: the fields are right, the program
runs, and the corruption surfaces later as a wrong virtual dispatch or a double
free somewhere unrelated. There is no diagnostic and no crash at the scene.

## Consequences

### Positive

- Invariants, vtable pointers and ownership counts survive a copy.
- Undefined behaviour that no sanitiser reliably catches is removed by
  construction.
- The type's author retains control over what copying means, rather than every
  call site deciding independently.

### Negative

- Writing the special member functions is more work than one `memcpy` line, and
  a type with many members pays that cost repeatedly.
- Element-wise copying can be slower than a single bulk `memcpy` for large
  trivially copyable arrays; the compiler usually recovers this, but not always,
  and the rule asks for the safe form before the fast one.
- Interop with C APIs that hand back structures to be zero-filled still requires
  `memset`, so the practice does not disappear — it moves to a boundary where it
  must be justified.

### Neutral

- Trivially copyable types are unaffected: for them the bitwise copy and the
  copy constructor mean the same thing.
- The rule says nothing about `std::memcmp`, though the same reasoning about
  padding bytes applies to comparing objects.

## Enforcement

- Flag passing a non-trivially-copyable type to `memset` or `memcpy`.

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — a
  bitwise copy of a class type is the canonical type-safety hole.
- [0014](0014-p-8-do-not-leak-any-resources.md) — a `memcpy`d smart pointer
  loses or double-frees its resource.
- [0105](0105-c-130-for-making-deep-copies-of-polymorphic-classes-prefer-a-virtual-clone-function-instead-of-public-copy-construction-assignment.md)
  — how a polymorphic object should be copied instead.
- [0039](0039-i-30-encapsulate-rule-violations.md) — where a byte-level
  operation is unavoidable, wrap it.
