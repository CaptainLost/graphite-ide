# 0162. R.23: Use make_unique() to make unique_ptrs

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

`make_unique` gives a more concise statement of the construction. It also
ensures exception safety in complex expressions.

```cpp
unique_ptr<Foo> p {new Foo{7}};    // OK: but repetitive
auto q = make_unique<Foo>(7);      // Better: no repetition of Foo
```

The explicit form is marked "OK" rather than "bad", because on its own it is
correct: the `unique_ptr` takes ownership in the same full expression, so
nothing leaks. The problem appears when that expression becomes a subexpression
of a larger one whose parts may be evaluated in an interleaved order, because
the allocation can then happen before some other subexpression fails and while
nothing yet owns the result. C++17 made function arguments indeterminately
sequenced, so the argument list this warning is usually illustrated with is no
longer such a context; the operands of most built-in binary operators still are.
`make_unique` has the problem in no context at all, because the allocation and
the ownership are inside one call.

Unlike `make_shared`, `make_unique` does not merge two allocations into one —
there is no control block — so its advantages are brevity, single naming of the
type, and the sequencing guarantee.

## Decision

Create a `unique_ptr` with `std::make_unique`.

- Write `auto p = std::make_unique<Foo>(args...)` rather than
  `std::unique_ptr<Foo> p{new Foo{args...}}`.
- Never write `std::unique_ptr<Foo>(new Foo{...})` as a subexpression of a
  larger one; give it a statement of its own, or use `make_unique` in place.
- Construct a `unique_ptr` from a raw pointer only where `make_unique` cannot
  express what is needed — a custom deleter, or adoption of a handle obtained
  elsewhere — and do so in a statement of its own, immediately after the
  acquisition.
- For an array whose elements are about to be overwritten, use
  `std::make_unique_for_overwrite` rather than paying for value initialisation.

## Rationale

The two forms are equally correct in isolation, so the case rests on what
happens as code changes: the explicit form is safe where it stands and can be
made unsafe by being folded into a larger expression, while `make_unique` cannot
be made unsafe that way in any context. Preferring the form that stays correct
under editing is worth more than the brevity, though the brevity is what will
actually get it adopted.

## Consequences

### Positive

- The type is named once, so the declared type and the allocated type cannot
  drift apart.
- The construction is safe in any expression context, including as a function
  argument.
- Reads as a single operation — make a `Foo`, own it — rather than as an
  allocation plus a wrapping.

### Negative

- `make_unique` cannot take a custom deleter, so every wrapper around an OS
  handle, a C library resource, or a `fopen` result still constructs its
  `unique_ptr` explicitly, and those are the cases most worth getting right.
- It cannot reach a private constructor without a friend declaration or a
  passkey, which complicates types that deliberately restrict construction.
- `auto q = make_unique<Foo>(7)` hides the pointer-ness of `q` at the
  declaration, which costs the reader something the explicit form gave away.
- It value-initialises, so allocating a large buffer that is about to be filled
  wastes a pass over it unless the `for_overwrite` variant is remembered.

### Neutral

- The gain here is smaller than for `make_shared`, because there is no second
  allocation to eliminate.
- Whether the object belongs on the free store at all is decided by another
  rule.

## Enforcement

- (Simple) Warn if a `unique_ptr` is constructed from the result of `new` rather
  than by `make_unique`.

## Related

- [0161](0161-r-22-use-make-shared-to-make-shared-ptrs.md) — the same rule for
  shared ownership, with the extra allocation argument.
- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — the rule this
  one makes practical.
- [0156](0156-r-13-perform-at-most-one-explicit-resource-allocation-in-a-single-expression-statement.md)
  — the expression-level hazard `make_unique` removes.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) — why
  there is a `unique_ptr` here at all.
