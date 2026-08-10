# 0459. F.26: Use a unique_ptr<T> to transfer ownership where a pointer is needed

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Using `unique_ptr` is the cheapest way to pass a pointer safely. Where a
function assembles an object and hands it to its caller, the return type can
carry the ownership rather than leaving it to a comment:

```cpp
unique_ptr<Shape> get_shape(istream& is)  // assemble shape from input stream
{
    auto kind = read_header(is); // read header and identify the next shape on input

    switch (kind) {
    case kCircle:
        return make_unique<Circle>(is);
    case kTriangle:
        return make_unique<Triangle>(is);
    // ...
    }
}
```

A pointer, rather than an object, is needed when what is being transferred is an
object from a class hierarchy that is to be used through an interface — that is,
through a base class. Returning `Shape` by value would slice it; returning
`Shape*` would transfer ownership without saying so. `unique_ptr<Shape>` is the
type that means exactly what is happening.

## Decision

Where ownership of a heap object must cross a function boundary and a pointer is
genuinely required, use `std::unique_ptr<T>`.

- Return `std::unique_ptr<T>` from a factory that produces a polymorphic object,
  rather than a raw `T*`.
- Take a `std::unique_ptr<T>` by value in a parameter to mean "this function
  takes ownership", and pass it with `std::move`.
- Construct with `std::make_unique<T>(...)` rather than `unique_ptr<T>(new T)`.
- Reach for a pointer only when polymorphism, optionality, or an existing
  hierarchy demands it; return a concrete object by value otherwise.
- Do not use `unique_ptr` in a parameter that merely observes or mutates the
  object. Take `T&` or `T*` for that, and leave ownership where it is.

## Rationale

The argument that settles this is that `unique_ptr` is not a safety tax paid
for with performance. It is the same pointer, moved rather than copied, with a
destructor attached; the ownership is enforced by the type system at no run-time
cost over the raw form.

Its second virtue is that the transfer becomes visible at both ends. A caller
of `get_shape` cannot forget to delete, and cannot accidentally keep a second
owner, because the language will not let a `unique_ptr` be copied. That
converts a documented obligation into a compile error.

## Consequences

### Positive

- The exception-safety of the transfer is automatic; an abandoned partial
  construction cannot leak.
- Ownership becomes non-duplicable by construction, so double-free and
  who-deletes-this questions disappear.
- A `unique_ptr` parameter documents a sink, and a `unique_ptr` return documents
  a source, without a comment.
- It converts cleanly to `shared_ptr` if requirements change later, but not the
  other way round, so it is the conservative default.

### Negative

- `unique_ptr` is not layout-compatible with a raw pointer for the purposes of a
  C ABI, so it stops at exactly the boundaries described in
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md).
- A `unique_ptr` parameter forces every call site to write `std::move`, and a
  moved-from argument is a live hazard for the rest of the caller's scope.
- It bakes the deleter into the type. Once a custom deleter is needed the type
  changes and every declaration mentioning it changes with it.
- The rule can be over-applied: it says use `unique_ptr` *where a pointer is
  needed*, and a heap allocation used only to avoid thinking about lifetime is
  still a heap allocation that
  [0015](0015-p-9-do-not-waste-time-or-space.md) objects to.

### Neutral

- The `switch` in the example above has no `default`; the upstream text elides
  the remaining cases and does not intend the fall-off-the-end path as advice.
- Whether a factory should return `unique_ptr` or `shared_ptr` depends on
  whether sharing is inherent to the object's use, which
  [0460](0460-f-27-use-a-shared-ptr-t-to-share-ownership.md) covers.

## Enforcement

- (Simple) Warn if a function returns a locally allocated raw pointer, and
  suggest `unique_ptr` or `shared_ptr` instead.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the interface rule that forbids the alternative.
- [0460](0460-f-27-use-a-shared-ptr-t-to-share-ownership.md) — the choice to
  make when ownership genuinely is shared.
- [0461](0461-f-42-return-a-t-to-indicate-a-position-only.md) — the complement:
  a returned raw pointer means position, never ownership.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the leak this rule prevents.
