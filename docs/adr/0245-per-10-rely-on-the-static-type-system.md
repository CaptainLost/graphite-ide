# 0245. Per.10: Rely on the static type system

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section Per (Performance), transcribed 2026-08-10

## Context

Type violations, weak types such as `void*`, and low-level code such as the
manipulation of sequences as individual bytes all make the optimiser's job much
harder. Simple code often optimises better than hand-crafted complex code.
Upstream gives this rule that reason and no example.

The mechanism is worth spelling out, because the rule reads like a safety
argument and is in fact a performance one. An optimiser transforms code only
where it can prove the transformation preserves behaviour, and most of those
proofs are made from types. Two pointers of unrelated types are assumed not to
alias, so stores through one need not be reloaded through the other; a
`std::vector<double>` is known to be contiguous and its elements known not to
alias each other's neighbours in surprising ways; a call to a known function can
be inlined and then constant-folded. Casting to `void*` or to `char*`, or
reaching for `reinterpret_cast`, withdraws those facts. The compiler must then
assume the worst, keep values in memory rather than in registers, and reload
after every opaque write.

A type violation is worse than an absence of information. Code whose behaviour
is undefined does not become slower in a predictable way; it becomes a program
the optimiser is entitled to transform on assumptions the code does not honour,
and the result is a fast program that is wrong.

So the static type system is not a tax paid for safety and refunded in
correctness. It is the channel through which the programmer's knowledge reaches
the code generator, and it is the same channel in both cases.

## Decision

Express what is known through types, and do not defeat them for speed.

- Keep interfaces specific. Prefer a typed reference, a `span`, or a container
  reference to `void*` plus a size, per
  [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md).
- Do not reinterpret memory as another type to process it faster. Accessing an
  object through a `reinterpret_cast` to an unrelated type violates the aliasing
  rules and gives undefined behaviour, and it is also an instruction to the
  optimiser to stop assuming things it was using.
- Do not manipulate a sequence as individual bytes when a typed view of it
  exists; use `std::byte`, `std::bit_cast`, or `memcpy` where a
  reinterpretation is genuinely required, and confine it.
- Prefer templates and concepts to type erasure where the type is known at the
  call site; a `void*` callback costs an indirect call the compiler cannot see
  through.
- Where a weakly typed interface is forced on the code from outside, convert to
  a typed representation immediately at the boundary and keep the rest of the
  code above it.

## Rationale

This is the rule that connects the performance section back to
[0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md): the same
declarations that make a program checkable make it optimisable, because both the
checker and the optimiser reason from the same facts.

The strongest form of the argument is that the usual trade is not available.
Someone who casts away a type to go faster is not buying speed with safety; they
are spending safety and, in the common case, getting a slower program in return
because the optimiser has lost the guarantees it was exploiting.

## Consequences

### Positive

- The optimiser keeps the aliasing and contiguity facts it needs to keep values
  in registers and to vectorise.
- Calls stay direct and inlinable, which enables constant folding across
  boundaries.
- The safety and the performance arguments point the same way, so there is no
  trade-off to negotiate in review.

### Negative

- Some work genuinely requires viewing memory as bytes — serialisation, file
  and network formats, GPU buffer uploads — and this rule makes that code
  awkward by design, pushing it into wrappers.
- Leaning on templates instead of type erasure trades run-time indirection for
  compile time and code size, and heavy instantiation slows builds noticeably.
- The rule is unenforceable as written: upstream gives it no check, and a cast
  that defeats it is a single well-formed-looking expression.

### Neutral

- Strong typing at interfaces is already required by
  [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md); this record
  adds the reason rather than the requirement.
- `std::bit_cast` and `memcpy` give defined ways to reinterpret representation
  and are not what this rule prohibits.

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the
  philosophy rule; this is its performance consequence.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  interface obligation this rule reinforces.
- [0242](0242-per-5-dont-assume-that-low-level-code-is-necessarily-faster-than-high-level-code.md)
  — the belief that leads to the type violations named here.
- [0244](0244-per-7-design-to-enable-optimization.md) — `void*` in an interface,
  and why it costs an implementation its future.
