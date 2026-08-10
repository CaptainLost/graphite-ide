# 0030. I.11: Never transfer ownership by a raw pointer (T*) or reference (T&)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

If there is any doubt whether the caller or the callee owns an object, leaks or
premature destruction will occur.

```cpp
X* compute(args)    // don't
{
    X* res = new X{};
    // ...
    return res;
}
```

Who deletes the returned `X`? The problem would be harder to spot if `compute`
returned a reference. Consider returning the result by value, using move
semantics if the result is large:

```cpp
vector<double> compute(args)  // good
{
    vector<double> res(10000);
    // ...
    return res;
}
```

Ownership can also be passed using a smart pointer — `unique_ptr` for exclusive
ownership, `shared_ptr` for shared ownership. However, that is less elegant and
often less efficient than returning the object itself, so smart pointers are for
when reference semantics are actually needed.

Sometimes older code cannot be modified, because of ABI compatibility
requirements or lack of resources. In that case, mark owning pointers using
`owner` from the guidelines support library:

```cpp
owner<X*> compute(args)    // It is now clear that ownership is transferred
{
    owner<X*> res = new X{};
    // ...
    return res;
}
```

This tells analysis tools that `res` is an owner: its value must be deleted or
transferred to another owner, as is done here by the return. `owner` is used
similarly in the implementation of resource handles.

Every object passed as a raw pointer, or as an iterator, is assumed to be owned
by the caller, so that its lifetime is handled by the caller. Viewed another
way: ownership-transferring APIs are relatively rare compared to pointer-passing
APIs, so the default is "no ownership transfer".

## Decision

Never transfer ownership by a raw pointer or a reference.

- Return by value. Let move semantics and copy elision handle the cost.
- Where reference semantics are genuinely required, transfer a `unique_ptr` or a
  `shared_ptr`, choosing which by whether ownership is exclusive or shared.
- A raw pointer or reference in an interface means "the caller owns this, and
  guarantees it outlives the call" — nothing else. It is a view, never a
  transfer.
- Where a raw owning pointer is unavoidable, at an ABI boundary or in code that
  cannot be changed, mark it with `owner` so that the transfer is at least
  stated and tool-visible.

## Rationale

Ownership is not visible in a `T*`, so an interface that transfers ownership
through one is a convention held in documentation and in memory. Every leak and
every double delete of this shape comes from two people holding different
conventions about the same signature.

Making the raw pointer mean exactly one thing — non-owning view — is what gives
the whole codebase a default. After that, the presence of a smart pointer or an
`owner` in a signature is itself information, because it is the only way
ownership moves.

## Consequences

### Positive

- Ownership is readable from the signature rather than from a comment.
- Return-by-value is usually the fastest option as well as the safest, thanks to
  moves and elision.
- Tools can check `owner`-marked code, which they cannot do for a bare `T*`.

### Negative

- Returning by value is not always possible: polymorphic results need a
  `unique_ptr`, which is the less elegant option the rule itself concedes.
- `shared_ptr` makes lifetime dynamic and cheap to reach for, and it is
  frequently reached for when exclusive ownership would have been correct.
- A C API boundary still needs the raw owning pointer, so the rule has a
  permanent exception at exactly the place mistakes are most expensive.

### Neutral

- This record does not say who should own what; it says how ownership is
  expressed once decided.
- Raw pointers remain entirely normal as non-owning parameters.

## Enforcement

- (Simple) Warn on `delete` of a raw pointer that is not an `owner<T>`. Suggest
  a standard-library resource handle or `owner<T>`.
- (Simple) Warn on failure to either reset or explicitly delete an owner pointer
  on every code path.
- (Simple) Warn if the return value of `new`, or of a function call with an
  owner return value, is assigned to a raw pointer or a non-owner reference.

## Related

- [0014](0014-p-8-do-not-leak-any-resources.md) — the philosophy rule this
  implements at interfaces.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  other thing a raw pointer fails to say.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — returning an object that carries both ownership and extent.
