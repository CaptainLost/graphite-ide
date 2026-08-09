# 0030. I.11: Never transfer ownership by a raw pointer or reference

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

The rule is stated upstream as "Never transfer ownership by a raw pointer (T*)
or reference (T&)"; the title here drops the parenthetical so that the filename
stays readable.

## Context

The codebase already complies. The one `new` in Core —
`DirectoryWatcher::Create` — hands its result straight to a `std::unique_ptr`
on the same line, and no function returns an owning raw pointer.

## Decision

Adopt I.11 as written. Transcribed in full:

> **Reason.** If there is any doubt whether the caller or the callee owns an
> object, leaks or premature destruction will occur.

**Example.** Consider:

```cpp
X* compute(args)    // don't
{
    X* res = new X{};
    // ...
    return res;
}
```

Who deletes the returned `X`? The problem would be harder to spot if `compute`
returned a reference. Consider returning the result by value (use move semantics
if the result is large):

```cpp
vector<double> compute(args)  // good
{
    vector<double> res(10000);
    // ...
    return res;
}
```

Alternative: Pass ownership using a "smart pointer", such as `unique_ptr` (for
exclusive ownership) and `shared_ptr` (for shared ownership). However, that is
less elegant and often less efficient than returning the object itself, so use
smart pointers only if reference semantics are needed.

Alternative: Sometimes older code can't be modified because of ABI compatibility
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

This tells analysis tools that `res` is an owner. That is, its value must be
deleted or transferred to another owner, as is done here by the return.

`owner` is used similarly in the implementation of resource handles.

**Note.** Every object passed as a raw pointer (or iterator) is assumed to be
owned by the caller, so that its lifetime is handled by the caller. Viewed
another way: ownership transferring APIs are relatively rare compared to
pointer-passing APIs, so the default is "no ownership transfer."

See also: Argument passing, use of smart pointer arguments, and value return.

**Enforcement.**

- (Simple) Warn on `delete` of a raw pointer that is not an `owner<T>`. Suggest
  use of standard-library resource handle or use of `owner<T>`.
- (Simple) Warn on failure to either reset or explicitly delete an owner pointer
  on every code path.
- (Simple) Warn if the return value of `new` or a function call with an `owner`
  return value is assigned to a raw pointer or non-owner reference.

## Rationale

Adopted as written and already satisfied, which makes it worth recording
precisely so that it stays satisfied. The Note carries the useful default: a raw
pointer in a signature means "the caller still owns this", and that convention
is what makes `Layer*` in `LayerStack` and `Panel*` in `PanelManager` readable
without further comment.

The `owner<T*>` alternative is unavailable — the GSL is declined by
[0019](0019-p-13-use-support-libraries-as-appropriate.md) — but it is the
alternative for code that *cannot* be changed, and none of this code is in that
position. Returning by value or by `unique_ptr` covers every case here.

## Consequences

### Positive

- Fixes the meaning of a raw pointer parameter across the codebase: observation,
  never ownership. That convention is what lets `LayerStack` hand out `Layer*`
  without ambiguity.
- No `delete` appears outside a resource handle, so the first enforcement point
  passes everywhere.

### Negative

- Without `owner<T*>`, an owning raw pointer — if one is ever forced by a C API —
  can only be documented in prose.
- `unique_ptr` in a signature is a commitment to heap allocation; the rule's own
  advice to prefer returning by value is sometimes at odds with the polymorphic
  types in `Platform` and `Ui`, which must be held by pointer.

### Neutral

- No `shared_ptr` is used anywhere in the repository, so the shared-ownership
  half of the rule has no subject.

## Related

- [0029](0029-i-10-use-exceptions-to-signal-a-failure-to-perform-a-required-task.md)
  — the previous rule.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  next rule.
- [0014](0014-p-8-do-not-leak-any-resources.md) — P.8, which this rule
  implements at the interface.
