# 0154. R.11: Avoid calling new and delete explicitly

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

The pointer returned by `new` should belong to a resource handle that can call
`delete`. If it is assigned to a plain pointer instead, the object can be
leaked.

In a large program a naked `delete` — one in application code, rather than in
code devoted to resource management — is a likely bug. If you have N deletes,
how can you be certain that you do not need N+1 or N-1? The bug might be latent,
and emerge only during maintenance. And if you have a naked `new`, you probably
need a naked `delete` somewhere, so you probably have a bug.

The counting argument is the whole of the rule. `new` and `delete` are correct
only in pairs, the pairing is established by control flow rather than by the
language, and control flow is what changes when code is maintained.

## Decision

Do not write `new` or `delete` in ordinary code.

- Create objects with `make_unique` or `make_shared`, or by putting them in a
  container, or by making them scoped objects.
- Let a handle's destructor do the deleting. A `delete` in application code is a
  defect report against whatever should have owned the pointer.
- Confine the remaining explicit `new` and `delete` to the implementation of a
  resource handle, a container, or an allocator — a small amount of code that
  exists to be reviewed as such.
- Where a factory returns a polymorphic object, return `unique_ptr<Base>` built
  by `make_unique<Derived>`, not a raw `new`.
- Placement `new`, a custom deleter, or a custom allocator may require the
  explicit form; wrap it rather than spreading it.

## Rationale

The rule is really about who counts. Deletion has to happen exactly once on
every path, and a naked `new`/`delete` pair asks a human to verify that property
across every branch, every early return and every exception, forever. A handle
discharges it once, in one place, by construction. That is why the guidelines
phrase the check as arithmetic: any code where the number of deletes is
something you have to work out is code where the number will eventually be
wrong.

## Consequences

### Positive

- The number of deletions stops being a property a reader must compute.
- Exception paths and early returns cease to be leak opportunities without
  anybody thinking about them.
- Ownership becomes visible in types, so review can be about design rather than
  about counting.

### Negative

- The rule cannot be absolute: containers, resource handles and allocators are
  built out of exactly the operations it forbids, so it displaces the hazard
  into a small body of code rather than eliminating it.
- `make_unique` and `make_shared` cannot express a custom deleter, so every
  OS-handle wrapper still constructs its `unique_ptr` from a raw value.
- Placement `new`, over-aligned allocation and arena allocation all require the
  explicit form, so a warning on any use of `new` or `delete` produces false
  positives in exactly the code that most needs review.
- An interface that must hand a pointer to a C API still ends with a raw
  pointer, and the release call on the far side is outside this rule's reach.

### Neutral

- This record does not choose between `unique_ptr` and `shared_ptr`; that is
  decided by whether ownership is shared.
- Avoiding `new` is not the same as avoiding heap allocation; not allocating at
  all is a stronger and often better answer.

## Enforcement

- (Simple) Warn on any explicit use of `new` and `delete`. Suggest `make_unique`
  instead.

## Related

- [0153](0153-r-10-avoid-malloc-and-free.md) — the same argument for the C pair.
- [0155](0155-r-12-immediately-give-the-result-of-an-explicit-resource-allocation-to-a-manager-object.md)
  — what to do on the occasions an explicit allocation is unavoidable.
- [0162](0162-r-23-use-make-unique-to-make-unique-ptrs.md) and
  [0161](0161-r-22-use-make-shared-to-make-shared-ptrs.md) — the replacements.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — where the surviving explicit allocations belong.
