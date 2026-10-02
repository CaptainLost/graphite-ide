# 0318. T.3: Use templates to express containers and ranges

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Containers need an element type, and expressing that as a template argument is
general, reusable, and type safe. It also avoids brittle or inefficient
workarounds. By convention, that is how the standard library does it.

```cpp
template<typename T>
    // requires Regular<T>
class Vector {
    // ...
    T* elem;   // points to sz Ts
    int sz;
};

Vector<double> v(10);
v[7] = 9.9;
```

The untyped alternative:

```cpp
class Container {
    // ...
    void* elem;   // points to size elements of some type
    int sz;
};

Container c(10, sizeof(double));
((double*) c.elem)[7] = 9.9;
```

This does not directly express the intent of the programmer, and it hides the
structure of the program from the type system and from the optimiser. Hiding the
`void*` behind macros simply obscures the problem and introduces new
opportunities for confusion.

The exception: if an ABI-stable interface is needed, a base implementation may
have to be provided with the type-safe template expressed in terms of it.

## Decision

Parameterise a container or range on its element type.

- Give a container a template type parameter for its elements and store `T`
  directly; do not store `void*` plus an element size.
- Constrain the element parameter — upstream's example asks for `Regular` — so
  that the container's requirements on `T` are stated.
- Do not cast through `void*` or a macro to recover the element type; outside
  low-level implementation code, that cast is the defect this rule exists to
  remove.
- Where an ABI-stable interface forces an untyped implementation, keep the
  untyped part as an internal base and expose only the template, per
  [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md).

## Rationale

The element type is the one piece of information a container cannot do without,
and erasing it into `void*` plus a byte count does not remove the requirement —
it just moves the obligation to remember it onto every call site, unchecked. The
optimiser loses as much as the type checker does: it cannot see the element type,
so it cannot see the layout, the alignment, or the triviality of the operations.

## Consequences

### Positive

- Element type errors are caught at compile time rather than producing a
  misinterpreted reinterpretation of memory.
- No casts at use sites, so the code reads as what it means.
- The optimiser sees concrete types and can inline and vectorise accordingly.
- The container matches standard-library convention, so standard algorithms and
  range facilities apply to it.

### Negative

- Every element type instantiates a fresh copy of the container's code, which
  costs compile time and binary size — exactly what the `void*` version was
  avoiding.
- A templated container cannot present a stable ABI, so the exception is not a
  corner case; it is the whole of any plugin or shared-library boundary.
- The container's implementation must be visible in a header, which exposes
  detail and couples rebuilds to it.
- Diagnostics from a deeply nested container instantiation are long, and remain
  so even with concepts.

### Neutral

- What the element concept should be — `Regular`, `semiregular`, or less — is
  decided by [0337](0337-t-46-require-template-arguments-to-be-at-least-semiregular.md),
  not here.
- The rule describes containers and ranges as a category; a fixed-purpose
  aggregate with one element type is a plain class, not a container.

## Enforcement

- Flag uses of `void*` and casts outside low-level implementation code.

## Related

- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the container
  conventions a templated container should follow.
- [0337](0337-t-46-require-template-arguments-to-be-at-least-semiregular.md) —
  what to require of the element type.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) and
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  the ABI exception this rule defers to.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — `void*`
  containers are the type-safety hole this closes.
