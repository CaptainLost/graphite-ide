# 0074. C.60: Make copy assignment non-virtual, take the parameter by const&, and return by non-const&

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The conventional signature is simple and efficient. To optimise for rvalues,
provide an overload taking a `&&` instead of complicating this one.

```cpp
class Foo {
public:
    Foo& operator=(const Foo& x)
    {
        // GOOD: no need to check for self-assignment (other than performance)
        auto tmp = x;
        swap(tmp); // see C.83
        return *this;
    }
    // ...
};

Foo a;
Foo b;
Foo f();

a = b;    // assign lvalue: copy
a = f();  // assign rvalue: potentially move
```

The copy-and-swap implementation technique offers the strong guarantee: the
copy happens first, and only a non-failing swap touches the target.

But what if significantly better performance is available by not making a
temporary copy? Consider a simple `Vector` intended for a domain where
assignment of large, equal-sized vectors is common. There the copy of elements
implied by the swap technique could cause an order-of-magnitude increase in
cost:

```cpp
template<typename T>
class Vector {
public:
    Vector& operator=(const Vector&);
    // ...
private:
    T* elem;
    int sz;
};

Vector& Vector::operator=(const Vector& a)
{
    if (a.sz > sz) {
        // ... use the swap technique, it can't be bettered ...
        return *this;
    }
    // ... copy sz elements from *a.elem to elem ...
    if (a.sz < sz) {
        // ... destroy the surplus elements in *this and adjust size ...
    }
    return *this;
}
```

Writing directly to the target elements gives only the basic guarantee rather
than the strong guarantee offered by the swap technique, and self-assignment
must then be considered explicitly.

If you think you need a virtual assignment operator, and understand why that is
deeply problematic, do not call it `operator=`. Make it a named function such as
`virtual void assign(const Foo&)`.

## Decision

Declare copy assignment as `T& operator=(const T&)`, non-virtual.

- Take the parameter by `const&`, return `T&` by non-const reference, and return
  `*this`.
- Do not declare `operator=` virtual. If a hierarchy needs polymorphic
  assignment, give it a named virtual function such as `assign` and leave
  `operator=` alone.
- Implement it by copy-and-swap unless a measurement shows the copy dominates;
  then write directly into the target and handle self-assignment deliberately.
- Add a `T& operator=(T&&)` overload for the rvalue case rather than trying to
  make the copy-assignment signature serve both.
- Ensure the operator assigns every base and every member, whether implicitly or
  explicitly.

## Rationale

Returning `T&` rather than `const T&` or `void` is what lets the type be used
generically: chaining, composability, and the standard library's container
requirements all assume it, and a type that returns something else fails to
compile in places far from its own definition.

Non-virtual is the load-bearing part. Assignment through a base reference cannot
be made to work sensibly in a hierarchy — the target's dynamic type may differ
from the source's, and there is no correct answer to what should happen — so the
right move is to not offer the operation under a name the language calls
implicitly.

## Consequences

### Positive

- The type composes: it can be put in containers, chained, and used by generic
  code without special-casing.
- Copy-and-swap gives the strong guarantee almost for free, and self-assignment
  correctness comes with it.
- Keeping `operator=` non-virtual removes a whole class of slicing and
  type-mismatch bugs from hierarchies.

### Negative

- Copy-and-swap allocates a whole new representation even when the existing one
  would fit, which for large equal-sized objects is an order of magnitude of
  waste — the guideline's own example concedes this.
- Optimising past that costs the strong guarantee and reintroduces the
  self-assignment obligation that copy-and-swap discharged.
- The prohibition on virtual assignment is not free either: a hierarchy that
  genuinely needs it must invent and document a named function, and callers must
  know to use it.

### Neutral

- The rule constrains the signature, not the semantics; what a copy must
  actually do is
  [0075](0075-c-61-a-copy-operation-should-copy.md).
- The exception-safety vocabulary here — basic and strong guarantee — describes
  what the operation leaves behind when it fails, not what it returns. An
  operator obliged to return `T&` has no channel for reporting a failure except
  throwing, which is a further reason to keep assignment from failing at all.

## Enforcement

- (Simple) An assignment operator should not be virtual. Here be dragons.
- (Simple) An assignment operator should return `T&` to enable chaining, not
  alternatives like `const T&`, which interfere with composability and with
  putting objects in containers.
- (Moderate) An assignment operator should, implicitly or explicitly, invoke all
  base and member assignment operators. Look at the destructor to determine
  whether the type has pointer semantics or value semantics.

## Related

- [0075](0075-c-61-a-copy-operation-should-copy.md) — what the operator with this
  signature is obliged to do.
- [0076](0076-c-62-make-copy-assignment-safe-for-self-assignment.md) — the
  obligation copy-and-swap discharges and the optimised form does not.
- [0077](0077-c-63-make-move-assignment-non-virtual-take-the-parameter-by-and-return-by-non-const.md)
  — the same rule for move assignment.
- [0085](0085-c-83-for-value-like-types-consider-providing-a-noexcept-swap-function.md)
  — the swap that the recommended implementation depends on.
