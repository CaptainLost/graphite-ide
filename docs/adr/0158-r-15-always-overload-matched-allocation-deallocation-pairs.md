# 0158. R.15: Always overload matched allocation/deallocation pairs

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Otherwise you get mismatched operations, and chaos.

```cpp
class X {
    // ...
    void* operator new(size_t s);
    void operator delete(void*);
    // ...
};
```

If you want memory that cannot be deallocated, `=delete` the deallocation
operation. Do not leave it undeclared.

The distinction matters because the two have different effects. An undeclared
class-scope `operator delete` is not an error: lookup simply proceeds to the
global one, so an object allocated by the class's own allocator is released by
an unrelated deallocator, quietly. A deleted one makes the same expression
ill-formed, which is the diagnostic you wanted.

The pair is really a family. Each form of allocation has a matching form of
deallocation — plain, array, `nothrow`, over-aligned, and the sized deallocation
the compiler may prefer — and declaring one member of the family hides the
inherited or global versions of the others for that class.

## Decision

Declare allocation and deallocation functions in matching pairs.

- If a class declares `operator new`, declare the corresponding `operator
  delete`; if it declares `operator new[]`, declare `operator delete[]`.
- Match the forms as well as the names: an aligned allocation needs the
  `std::align_val_t` deallocation, a `nothrow` allocation needs the `nothrow`
  deallocation, and a sized deallocation must be consistent with the allocation
  it will be paired with.
- To forbid a deallocation, write `= delete` for it. Never leave it undeclared
  and rely on the global one not being found.
- Keep the pair together, in the same class and next to each other, so that a
  later edit cannot add one without seeing the other.

## Rationale

An allocation function without its partner is worse than no overload at all,
because the mismatch it creates is silent. The declaration removes the class's
allocation from the global scheme but leaves its deallocation in it, so every
`delete` of the type returns storage to an allocator that never issued it — a
heap corruption whose symptom appears somewhere else entirely. That the fix is
one declaration, and the failure is undebuggable, is the whole argument.

## Consequences

### Positive

- Storage is always returned to the allocator that issued it.
- Forbidding deallocation becomes a compile error rather than a silent fallback
  to the global operator.
- The pair being adjacent makes the omission visible in review.

### Negative

- The "pair" is a family that has grown across standard versions — array,
  `nothrow`, sized, aligned, class-specific placement forms — so a class can
  satisfy the rule as literally stated and still leave a form unmatched.
- Class-scope deallocation is selected through the object's dynamic type in a
  hierarchy, so a polymorphic class needs a virtual destructor for its custom
  deallocator to be reached at all; the rule does not mention this.
- Declaring these operators is itself something most code should not do, so a
  rule about doing it well can read as encouragement to do it.
- Compilers may elide or coalesce allocations, so a custom pair does not
  necessarily see every allocation of the type.

### Neutral

- The rule applies to class-scope and global replacements alike.
- Whether a custom allocator is a good idea is decided elsewhere; this rule
  applies once it exists.

## Enforcement

- Flag incomplete pairs.

## Related

- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — most code
  should not be in this territory at all.
- [0153](0153-r-10-avoid-malloc-and-free.md) — the same mismatch, between the C
  and C++ allocation schemes.
- [0039](0039-i-30-encapsulate-rule-violations.md) — a custom allocator is
  exactly the kind of thing that belongs behind an interface.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — keep the pair in one place.
