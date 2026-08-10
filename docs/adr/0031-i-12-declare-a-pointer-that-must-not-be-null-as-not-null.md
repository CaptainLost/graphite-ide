# 0031. I.12: Declare a pointer that must not be null as not_null

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Declaring a pointer as `not_null` helps avoid dereferencing null, and improves
performance by avoiding redundant checks for null.

```cpp
int length(const char* p);            // it is not clear whether length(nullptr)
                                      // is valid
length(nullptr);                      // OK?

int length(not_null<const char*> p);  // better: we can assume that p cannot be
                                      // nullptr
int length(const char* p);            // we must assume that p can be nullptr
```

By stating the intent in source, implementers and tools can provide better
diagnostics — finding some classes of error through static analysis — and
perform optimisations, such as removing branches and null tests.

`not_null` is defined in the guidelines support library.

The assumption that a pointer to `char` pointed to a C-style string — a
zero-terminated string of characters — is still implicit in the example above,
and a potential source of confusion and errors. Use `czstring` in preference to
`const char*`:

```cpp
// we can assume that p cannot be nullptr
// we can assume that p points to a zero-terminated array of characters
int length(not_null<zstring> p);
```

`length()` is, of course, `std::strlen()` in disguise.

## Decision

Declare a pointer that must not be null as `not_null`, and let a plain pointer
mean that null is a legitimate value the callee handles.

- A pointer parameter that the function will dereference unconditionally is
  `not_null`.
- A plain `T*` in an interface is a statement that null is acceptable and
  handled; if the function immediately checks and rejects null, the parameter
  should have been `not_null`.
- Where a pointer means a zero-terminated string, say so with `zstring` or
  `czstring` rather than `char*`.
- Prefer a reference where the parameter is neither optional nor rebindable and
  no pointer semantics are needed at all.

## Rationale

Nullability is the second thing a raw pointer fails to state, after ownership.
The result is a defensive check at every level of a call chain, each written by
someone who did not know whether the level above had already checked — cost
without confidence.

Putting the answer in the type collapses that: the check happens once, at the
boundary where the pointer is constructed, and every function downstream can
dereference without asking. The performance argument is real but secondary; the
diagnostic one is the point.

## Consequences

### Positive

- Whether null is allowed becomes part of the contract instead of a guess.
- Redundant null checks disappear, along with the branches they generate.
- Static analysis can find some classes of null-dereference error.

### Negative

- `not_null` moves the check to construction; it does not remove it. Constructing
  one from a pointer that is null is a runtime failure, so the failure mode is
  relocated rather than eliminated.
- It is a wrapper type, so it appears in signatures, in headers, and in every
  file that includes them, and it requires the supporting library.
- Retrofitting it to existing interfaces is a wide, mechanical change with no
  behavioural benefit until the checks are removed.

### Neutral

- This record does not adopt any particular library into the build; it states
  the convention.
- Where a reference is the better answer, that is a different rule and not a
  violation of this one.

## Enforcement

- (Simple) If a function checks a pointer parameter against `nullptr` before
  access on all control-flow paths, warn that it should be declared `not_null`.
- (Complex) If a function with a pointer return value ensures it is not
  `nullptr` on all return paths, warn that the return type should be declared
  `not_null`.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the ownership half of what a raw pointer does not say.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the extent
  half.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — a stronger
  type carrying a stated guarantee.
