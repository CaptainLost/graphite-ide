# 0151. R.5: Prefer scoped objects, don't heap-allocate unnecessarily

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

A scoped object is a local object, a global object, or a member. Being scoped
implies that there is no separate allocation and deallocation cost beyond the
one already paid for the containing scope or object. The members of a scoped
object are themselves scoped, and the containing object's constructor and
destructor manage their lifetimes.

The following is inefficient, because of the unnecessary allocation and
deallocation; vulnerable to a throw or a return in the `...` part, which leaks;
and verbose:

```cpp
void f(int n)
{
    auto p = new Gadget{n};
    // ...
    delete p;
}
```

A local variable says the same thing with none of that:

```cpp
void f(int n)
{
    Gadget g{n};
    // ...
}
```

## Decision

Give an object automatic storage duration unless something forces otherwise.

- Make an object a local variable or a member by default. Reach for the free
  store only when the object must outlive the scope, must be polymorphic, must
  have a stable address across container growth, or is too large for the stack.
- Do not allocate an object at the top of a function and release it at the
  bottom; that is a local variable written the hard way.
- A member that is always present is a member, not a `unique_ptr` member.
- Where the free store is genuinely required, own the allocation with a handle
  rather than a raw pointer.

## Rationale

The heap allocation in the bad example buys nothing: the object's lifetime is
exactly the scope either way, so the program pays an allocator round trip, a
deallocation, worse locality and an extra indirection for no change in
behaviour. Worse, it converts a lifetime the compiler guarantees into one the
programmer must guarantee, which is what the early return in the `...` part
exploits. Automatic storage is the cheapest correct answer and also the fastest
one.

## Consequences

### Positive

- Removes an allocation and a deallocation, and with them a failure mode.
- Destruction is guaranteed by the language on every path, so this is the least
  effortful form of the leak rule.
- Better locality and one less indirection, at no cost in expressiveness for the
  common case.

### Negative

- The stack is small and its exhaustion is not a recoverable error, so a large
  or recursively nested object must go on the heap, and where the boundary lies
  is a judgement.
- Scoped lifetime is inflexible: an object that turns out to need to outlive its
  scope has to be converted, and every reference to it revisited.
- Polymorphism, optional members and stable addresses all need indirection, so
  the rule quietly does not apply to a large part of an object-oriented design.
- The guidelines count a global object as scoped, which sits badly against the
  rule against non-`const` globals; "scoped" here means "storage is managed by
  its containing scope", not "local", and the two rules must both be satisfied.

### Neutral

- This record is about where an object lives, not about who may see it.
- A scoped object holding a handle to a heap allocation, such as a local
  `std::vector`, satisfies this rule; the allocation is the container's
  business.

## Enforcement

- (Moderate) Warn if an object is allocated and then deallocated on all paths
  within a function. Suggest a local automatic object instead.
- (Simple) Warn if a local unique or shared pointer is not moved, copied,
  reassigned or reset before its lifetime ends.

## Related

- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — a scoped object is the simplest resource handle there is.
- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — the allocation
  this rule removes entirely rather than wrapping.
- [0152](0152-r-6-avoid-non-const-global-variables.md) — the limit on the
  "global object" half of "scoped".
- [0015](0015-p-9-do-not-waste-time-or-space.md) — an unnecessary allocation is
  waste of exactly the kind that rule means.
