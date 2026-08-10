# 0187. ES.24: Use a unique_ptr<T> to hold pointers

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

`std::unique_ptr` is the simplest way to avoid leaks. It is reliable, it makes
the type system do much of the work of validating ownership safety, it increases
readability, and it has zero or near-zero run-time cost.

```cpp
void use(bool leak)
{
    auto p1 = make_unique<int>(7);   // OK
    int* p2 = new int{7};            // bad: might leak
    // ... no assignment to p2 ...
    if (leak) return;
    // ... no assignment to p2 ...
    vector<int> v(7);
    v.at(7) = 0;                    // exception thrown
    // ...
}
```

If `leak == true` the object pointed to by `p2` is leaked and the object pointed
to by `p1` is not. The same holds when `at()` throws. The two exits are
different in kind — one is an ordinary `return` that a reader can see, the other
is an exception thrown from a call that does not look like it exits the function
at all — and `p1` survives both without anyone having thought about either.

The scope of the rule is ownership. A raw pointer that merely refers to
something owned elsewhere is not what this rule is about; a raw pointer that is
the target of `new`, `malloc()`, or a function returning freshly allocated
storage is.

## Decision

Hold every owning pointer in a `unique_ptr`.

- Create objects with `std::make_unique<T>(...)`, not with a bare `new` assigned
  to a raw pointer.
- Where a custom release is needed, give the `unique_ptr` a deleter rather than
  writing a matching free call by hand.
- Use `std::shared_ptr` only where ownership is genuinely shared, and prefer
  `unique_ptr` until that is demonstrated.
- Keep raw pointers and references for non-owning observation only, and let them
  say so by not being the thing that was allocated.
- When wrapping a C API that hands back an allocated resource, take ownership at
  the boundary — a `unique_ptr` with the API's release function as its deleter —
  rather than tracking the release in the calling code. This follows
  [0039](0039-i-30-encapsulate-rule-violations.md).

## Rationale

The argument is not that a leak is expensive; it is that the correctness of a
raw owning pointer depends on every exit path from the function, and exit paths
are added by later edits and by calls that throw. Manual release is a proof
obligation that grows with the function and is discharged nowhere in the type
system. `unique_ptr` converts it into a property of the type, which is checked
once, by the compiler, at zero cost.

Note that avoiding exceptions does not remove the obligation. An early `return`
on a failure path leaks a raw owning pointer exactly as a `throw` does, so code
that reports errors by return value needs `unique_ptr` for the same reason and
to the same extent.

## Consequences

### Positive

- Every exit path releases the resource, including exits added later by someone
  who did not read the function's top.
- Ownership becomes visible in the signature: a function returning
  `unique_ptr<T>` transfers, a function taking `T*` does not.
- The cost is zero or near zero, so there is no efficiency argument against it.

### Negative

- `unique_ptr` implies a heap allocation and a pointer indirection at every
  use site; where a value member or a container would do, reaching for
  `unique_ptr` is a worse answer than the raw pointer it replaced.
- It does not cover ownership of things that are not pointers — file
  descriptors, handles, GPU resources — without a custom deleter type per
  resource, which is more ceremony than the rule suggests.
- Interoperating with C APIs requires `.get()` and `.release()` at the boundary,
  and `.release()` reintroduces exactly the unowned raw pointer the rule exists
  to remove.
- Storing `unique_ptr` in a container makes the container move-only, which
  propagates a constraint outwards from a local ownership decision.

### Neutral

- The rule concerns owning pointers. Non-owning raw pointers and references are
  unaffected by it.
- `shared_ptr` is not forbidden; it is simply not the default.

## Enforcement

Look for raw pointers that are targets of `new`, `malloc()`, or functions that
might return such pointers.

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the philosophy rule this one
  implements for pointers.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the interface counterpart: `unique_ptr` is how transfer is spelled.
- [0039](0039-i-30-encapsulate-rule-violations.md) — where a C API forces
  `.get()` and `.release()`, confine it.
- [0173](0173-es-2-prefer-suitable-abstractions-to-direct-use-of-language-features.md)
  — `unique_ptr` is the abstraction that fixes the leaking example there.
