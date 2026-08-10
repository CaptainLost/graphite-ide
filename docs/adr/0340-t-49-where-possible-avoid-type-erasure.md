# 0340. T.49: Where possible, avoid type-erasure

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

**This rule is nearly empty upstream.** Its Reason is a single sentence, its
Example is a bare `???` placeholder, and its Enforcement is another. What can be
transcribed is only this: type erasure incurs an extra level of indirection by
hiding type information behind a separate compilation boundary. There is one
stated exception — type erasure is sometimes appropriate, as for `std::function`.

Everything the rule leaves unwritten has to be supplied by the reader, so this
record deliberately keeps its claims to what the one sentence supports. The
mechanism it describes appears in
[0320](0320-t-5-combine-generic-and-oo-techniques-to-amplify-their-strengths-not-their-costs.md),
whose "dynamic helps static" example builds exactly such a wrapper: a template
constructor that stores a `shared_ptr` to an abstract base, so that many types
present as one. That example shows both what the technique buys — a single type
at the boundary, with value semantics for the caller — and what it costs, an
allocation and a virtual call per operation.

## Decision

Treat type erasure as a technique with a price, chosen deliberately rather than
by default.

- Prefer a template parameter, so the concrete type is visible at the point of
  use and the call can be inlined.
- Use an erasing wrapper only where a single runtime type is genuinely required:
  a heterogeneous container, a stored callback, a stable boundary, or an
  interface that must not be a template.
- Where erasure is used, keep it at the boundary rather than letting it spread
  inwards, so the indirection is paid once instead of on every internal call.
- Do not take a `std::function` parameter for a callable that is invoked
  immediately; take a constrained template parameter, per
  [0332](0332-t-40-use-function-objects-to-pass-operations-to-algorithms.md).

## Rationale

The one argument the rule makes is about the compilation boundary rather than
about the indirection alone: erasing a type means the caller's type is unknown
where the operation is compiled, so the optimiser cannot inline, devirtualise or
specialise anything across that boundary, and the cost is structural rather than
a matter of one extra pointer hop. The exception is equally instructive —
`std::function` is worth its cost precisely because a stored callback must have a
type that does not depend on what was stored.

## Consequences

### Positive

- Calls stay direct and inlinable, so generic code keeps the performance that
  motivates it.
- The concrete type remains visible to the compiler and to the reader.
- Allocations hidden inside erasing wrappers are avoided.

### Negative

- Avoiding erasure pushes types into signatures, so interfaces become templates,
  definitions move into headers, and compilation is slower and more coupled —
  the exact trade
  [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) resolves in
  the opposite direction.
- Heterogeneous storage becomes awkward without some erasure, whether a variant,
  a base class, or a wrapper.
- The rule offers no test for "where possible", and with no example and no
  enforcement upstream, it is a preference rather than a check.
- Applied too eagerly it argues against `std::function`, `std::any` and pimpl,
  all of which are the right answer in their place.

### Neutral

- The rule does not prohibit erasure; it sets the default and names an exception.
- Where a stable ABI is required, erasure of some kind is unavoidable and
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md)
  governs instead.

## Related

- [0320](0320-t-5-combine-generic-and-oo-techniques-to-amplify-their-strengths-not-their-costs.md)
  — the erasure example this rule qualifies.
- [0248](0248-per-13-eliminate-redundant-indirections.md) — the indirection cost,
  stated as a performance rule.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — a case
  where hiding the type behind a compilation boundary is the point.
- [0332](0332-t-40-use-function-objects-to-pass-operations-to-algorithms.md) —
  passing a constrained callable rather than an erased one.
