# 0038. I.27: For stable library ABI, consider the Pimpl idiom

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Because private data members participate in class layout, and private member
functions participate in overload resolution, changes to those implementation
details require recompilation of all users of a class that uses them. A
non-polymorphic interface class holding a pointer to implementation — Pimpl —
can isolate the users of a class from changes in its implementation, at the cost
of an indirection.

The interface, in `widget.h`:

```cpp
class widget {
    class impl;
    std::unique_ptr<impl> pimpl;
public:
    void draw(); // public API that will be forwarded to the implementation
    widget(int); // defined in the implementation file
    ~widget();   // defined in the implementation file, where impl is a complete
                 // type
    widget(widget&&); // defined in the implementation file
    widget(const widget&) = delete;
    widget& operator=(widget&&); // defined in the implementation file
    widget& operator=(const widget&) = delete;
};
```

The implementation, in `widget.cpp`:

```cpp
class widget::impl {
    int n; // private data
public:
    void draw(const widget& w) { /* ... */ }
    impl(int n) : n(n) {}
};

void widget::draw() { pimpl->draw(*this); }
widget::widget(int n) : pimpl{std::make_unique<impl>(n)} {}
widget::widget(widget&&) = default;
widget::~widget() = default;
widget& widget::operator=(widget&&) = default;
```

Note the shape this forces: the destructor and the move operations must be
declared in the header and defined in the implementation file, where `impl` is a
complete type. See GOTW #100 and cppreference for the trade-offs and further
details.

## Decision

Where a class's ABI must stay stable across changes to its implementation, use
the Pimpl idiom.

- The header declares the public API and a single `std::unique_ptr<impl>`
  member; no other data members, no private member functions.
- The destructor and the move operations are declared in the header and defined
  in the implementation file, where `impl` is complete.
- Copy operations are deleted unless copying is genuinely wanted, in which case
  they are written explicitly.
- Do not apply this to types that are not ABI boundaries. The indirection, the
  allocation and the forwarding are a real cost, paid on every use.

## Rationale

The problem is that C++ leaks private implementation details into the class's
layout and into its overload set, so "private" is a visibility rule, not an
isolation one. Pimpl restores the isolation by making the layout a single
pointer that never changes.

The narrow scoping is the important part of the decision. The idiom costs an
allocation, an indirection, and a header that no longer allows inlining, which
is a bad trade for the great majority of types — those compiled together with
everything that uses them, where the only cost of a layout change is a rebuild.

## Consequences

### Positive

- Implementation changes do not force recompilation of users, and do not break a
  shipped ABI.
- Header dependencies shrink: the implementation's includes stay out of the
  header.
- Private details are genuinely private, not merely inaccessible.

### Negative

- An allocation per object, an indirection per call, and no inlining across the
  boundary.
- The special member functions must be declared and defined in specific places,
  and getting that wrong produces incomplete-type errors that are confusing out
  of context.
- Every public function needs a forwarding stub, which is code that exists only
  to be typed.

### Neutral

- This addresses recompilation and layout stability for one compiler; a
  cross-compiler ABI is a different and stricter problem, and is
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md).
- The idiom is a suggestion — "consider" — not a requirement for library types.

## Enforcement

(Not enforceable.) It is difficult to reliably identify where an interface forms
part of an ABI.

## Related

- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  the cross-compiler case.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the other way to keep representation out of an interface.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — the general principle of keeping implementation from leaking outward.
