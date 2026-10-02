# 0159. R.20: Use unique_ptr or shared_ptr to represent ownership

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

They can prevent resource leaks.

```cpp
void f()
{
    X x;
    X* p1 { new X };              // bad: raw owning pointer
    unique_ptr<X> p2 { new X };   // unique ownership
    shared_ptr<X> p3 { new X };   // shared ownership
    auto p4 = make_unique<X>();   // unique ownership, preferable to the
                                  // explicit use of "new"
    auto p5 = make_shared<X>();   // shared ownership, preferable to the
                                  // explicit use of "new"
}
```

This leaks the object used to initialise `p1`, and only that one. Every other
line names a type that deletes what it holds when the scope ends, whether the
scope ends normally or through an exception.

The first line is worth as much attention as the last. `X x;` is not a
compromise: it is the best of the six, because it needs no allocation and no
handle. The rule is about what to do when an object must be on the free store,
not an argument for putting it there.

## Decision

When ownership of a heap object is needed, express it with a smart pointer.

- Use `std::unique_ptr<T>` for exclusive ownership and `std::shared_ptr<T>` for
  ownership genuinely shared between several owners with no single lifetime.
- Never let a raw pointer be the only thing keeping an object alive.
- Prefer `make_unique` and `make_shared` to constructing a smart pointer from
  `new`.
- Where the resource is not a heap object — an OS handle, a lock, a file — use
  the handle type that fits it, or write one; a smart pointer with a custom
  deleter is a fallback, not the first answer.
- Consider first whether the object needs to be on the free store at all.

## Rationale

The value of the rule is that ownership becomes a property of the type rather
than of the code around it. A `unique_ptr<X>` deletes its object once, on every
path, and no future edit can change that; a `X*` deletes its object only for as
long as everybody who touches the function remembers to. Because the guarantee
is in the type, it also travels: passing, returning and storing the handle
carries the ownership with it, which a comment attached to a `X*` cannot do.

## Consequences

### Positive

- Leaks from early returns and exceptions become impossible rather than
  unlikely.
- Ownership is readable from a declaration and enforced by the compiler.
- The smart pointer is the same size as the raw pointer in the `unique_ptr` case
  with the default deleter, so the guarantee is usually free.

### Negative

- A smart pointer implies an allocation, which conflicts with the better advice
  of preferring a scoped object; reaching for `make_unique` reflexively produces
  heap traffic that was never needed.
- `shared_ptr` costs an atomic increment and decrement per copy and a separate
  control block, and it makes lifetime dynamic and therefore harder to reason
  about than a scope.
- Non-memory resources fit awkwardly: expressing an OS handle as a smart pointer
  requires a deleter type and often a pointer that is not a pointer, which is
  more obscure than a small purpose-built RAII class.
- A smart pointer in a signature is a statement about lifetime, so overusing
  them as ordinary parameters pessimises interfaces that only needed a
  reference.

### Neutral

- Choosing between `unique_ptr` and `shared_ptr` is the next rule's subject.
- Owning raw pointers remain necessary inside the implementations of these
  handles themselves.

## Enforcement

- (Simple) Warn if the return value of `new`, or of a function call returning a
  pointer, is assigned to a raw pointer.

## Related

- [0149](0149-r-3-a-raw-pointer-a-t-is-non-owning.md) — the rule that leaves a
  gap this one fills.
- [0160](0160-r-21-prefer-unique-ptr-over-shared-ptr-unless-you-need-to-share-ownership.md)
  — which of the two to use.
- [0151](0151-r-5-prefer-scoped-objects-dont-heap-allocate-unnecessarily.md) —
  the option that is better than both.
- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — when a smart pointer should not appear in a signature.
