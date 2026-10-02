# 0353. T.84: Use a non-template core implementation to provide an ABI-stable interface

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Putting the real work in a non-templated core improves the stability of code
and avoids code bloat. The core can be a base class:

```cpp
struct Link_base {   // stable
    Link_base* suc;
    Link_base* pre;
};

template<typename T>   // templated wrapper to add type safety
struct Link : Link_base {
    T val;
};

struct List_base {
    Link_base* first;   // first element (if any)
    int sz;             // number of elements
    void add_front(Link_base* p);
    // ...
};

template<typename T>
class List : List_base {
public:
    void put_front(const T& e) { add_front(new Link<T>{e}); }   // implicit cast to Link_base
    T& front() { return static_cast<Link<T>*>(first)->val; }    // explicit cast back to Link<T>
    // ...
};

List<int> li;
List<string> ls;
```

There is now only one copy of the operations that link and unlink elements of a
`List`. The `Link` and `List` class templates do nothing but type manipulation:
they add the static type safety that `List_base` cannot express, and they
compile away.

Instead of using a separate base type, another common technique is to
specialize for `void` or `void*` and have the general template for `T` be
nothing but the safely encapsulated casts to and from the core `void`
implementation. A pimpl implementation is a further alternative, where the
stable part is a pointed-to implementation object rather than a base.

## Decision

Where a class template's behaviour does not really depend on its parameter,
write that behaviour once in a non-templated core.

- Put the state and the operations that manipulate it into a non-templated base
  class or implementation class, compiled into one translation unit.
- Make the class template a thin wrapper whose members do nothing but convert
  between the typed and the untyped views.
- Confine every cast to the wrapper, so that the unchecked step is in one small,
  reviewable place.
- Where the wrapper would otherwise expose the base's untyped operations,
  inherit privately and re-expose only the typed ones.
- Consider the `void*` specialization or the pimpl idiom where a base class is
  the wrong shape.

## Rationale

The template parameter of a node-based container affects the element's storage
and nothing else — the pointer manipulation is identical for every `T` — so
instantiating it per type buys nothing and costs a copy of the code in every
instantiation. Isolating the type-independent part also isolates the part whose
binary layout must not change, which is what makes an ABI-stable interface
possible at all: the wrapper can be recompiled freely because it exists only in
the caller's translation unit.

## Consequences

### Positive

- One copy of the shared logic, however many instantiations exist.
- The stable core can be changed behind a fixed interface, and callers only
  need recompiling when the core's layout changes.
- The type-safe wrapper still gives callers the checking they expect, at no run
  time cost.

### Negative

- The casts are the price. `static_cast<Link<T>*>(first)` is unchecked, and the
  design is correct only because the wrapper is the only thing that ever puts
  nodes into the core; a second entry point breaks it silently.
- The core loses the type information, so it cannot construct, copy or destroy
  elements without help, which usually means function pointers, a virtual hook,
  or restricting the core to raw pointer bookkeeping.
- Type safety is now a property of the wrapper's implementation rather than of
  the language, so a bug there is a memory-safety bug rather than a compile
  error.
- Two classes exist where one did, and the split has to be maintained as
  requirements grow — a member that suddenly needs the element type has to move
  across the boundary.

### Neutral

- The three forms — base class, `void*` specialization, pimpl — differ in where
  the indirection sits, not in the underlying idea.
- Upstream offers no enforcement for this rule; it is a design technique, not a
  check.

## Related

- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  alternative form named by this rule.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  what ABI stability costs once it must survive a change of compiler.
- [0343](0343-t-62-place-non-dependent-class-template-members-in-a-non-templated-base-class.md)
  — the same move made for usability rather than for stability.
- [0349](0349-t-80-do-not-naively-templatize-a-class-hierarchy.md) — the problem
  this technique solves.
