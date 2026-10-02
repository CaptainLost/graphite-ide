# 0100. C.122: Use abstract classes as interfaces when complete separation of interface and implementation is needed

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Complete separation is needed on a boundary such as an ABI or link boundary,
where the two sides are built separately and must remain compatible.

```cpp
struct Device {
    virtual ~Device() = default;
    virtual void write(span<const char> outbuf) = 0;
    virtual void read(span<char> inbuf) = 0;
};

class D1 : public Device {
    // ... data ...
    void write(span<const char> outbuf) override;
    void read(span<char> inbuf) override;
};

class D2 : public Device {
    // ... different data ...
    void write(span<const char> outbuf) override;
    void read(span<char> inbuf) override;
};
```

A user can now use `D1`s and `D2`s interchangeably through the interface
provided by `Device`. Furthermore, `D1` and `D2` can be updated in ways that are
not binary compatible with older versions, as long as all access goes through
`Device`.

The second sentence is the one that distinguishes this rule from
[0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md).
That record says what an interface should look like; this one says when the
separation is worth paying for. The condition is that the implementation's
representation must be free to change without the user recompiling — because the
user is a different binary, a different build, or a plugin loaded at run time.
An abstract class achieves that because its layout is just a vtable pointer: the
caller's code depends on the order of the virtual functions and nothing else.

Note also the shape of the interface in the example. `Device` traffics in
`span`, not in `D1`'s or `D2`'s own types, because a type that appears in the
interface is part of the contract and must be as stable as the interface itself.

## Decision

Use an abstract class as the interface where implementation and user are
separately compiled or separately deployed.

- Apply this where the boundary is real: a dynamically loaded plugin, a
  separately versioned library, or a seam across which the implementation must
  be replaceable at run time.
- Make the interface a pure abstract class with a virtual destructor and no
  data, per
  [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md).
- Use only types in the interface that both sides agree on and that are
  themselves stable.
- Hand out implementations through a factory returning a pointer to the
  interface, so that the concrete type never appears in the user's translation
  unit.
- Once published, add virtual functions only at the end of the interface, and
  do not reorder or remove existing ones.
- Do not pay this cost where the implementation and the user are compiled
  together and a concrete class, a template, or a `variant` would do.

## Rationale

The benefit is asymmetric and specific: it is not abstraction for its own sake,
it is the ability to ship a new implementation without rebuilding the caller. If
that ability is not needed, the indirection buys nothing and costs a vtable, a
heap allocation and a devirtualisation opportunity on every call.

Recognising this is what keeps a codebase from acquiring an interface for every
class. The question to ask is not "might there be another implementation one
day?" but "would the caller have to be recompiled if there were?" — and inside a
single build, the answer is that recompiling is free.

## Consequences

### Positive

- The implementation's representation can change without recompiling the user.
- Implementations become substitutable, which is also what makes them testable
  with a fake.
- The concrete type stays out of the user's translation unit, so its headers and
  dependencies do not leak.

### Negative

- Every call is a virtual call, and the compiler cannot inline across the
  boundary, so this is exactly the cost the separation buys.
- Objects must be heap-allocated and handed out by pointer; they cannot be
  stack values, which pulls ownership questions into every user.
- The interface itself becomes frozen: adding, removing or reordering a virtual
  function is an ABI break, so the interface has to be right the first time.
- C++ has no stable cross-compiler ABI for vtables, so this separation holds
  only within one compiler and one set of build settings.

### Neutral

- Pimpl, per [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md),
  solves an overlapping problem when there is only ever one implementation.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md)
  applies when the boundary must also cross compilers.

## Related

- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the shape the interface must take.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  alternative when there is a single implementation.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  the limit of what an abstract class can do across compilers.
- [0107](0107-c-132-dont-make-a-function-virtual-without-reason.md) — the
  counterweight against reaching for virtual by default.
