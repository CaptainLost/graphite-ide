# 0032. I.13: Do not pass an array as a single pointer

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The interface-level statement of what P.5, P.6 and P.7 argue for, and the rule
that `std::span` exists to satisfy. `<span>` is in `corepch.h` and used nowhere.

## Decision

Adopt I.13 as written. Transcribed in full:

> **Reason.** (pointer, size)-style interfaces are error-prone. Also, a plain
> pointer (to array) must rely on some convention to allow the callee to
> determine the size.

**Example.** Consider:

```cpp
void copy_n(const T* p, T* q, int n); // copy from [p:p+n) to [q:q+n)
```

What if there are fewer than `n` elements in the array pointed to by `q`? Then,
we overwrite some probably unrelated memory. What if there are fewer than `n`
elements in the array pointed to by `p`? Then, we read some probably unrelated
memory. Either is undefined behavior and a potentially very nasty bug.

**Alternative.** Consider using explicit spans:

```cpp
void copy(span<const T> r, span<T> r2); // copy r to r2
```

**Example, bad.** Consider:

```cpp
void draw(Shape* p, int n);  // poor interface; poor code
Circle arr[10];
// ...
draw(arr, 10);
```

Passing 10 as the `n` argument might be a mistake: the most common convention is
to assume `[0:n)` but that is nowhere stated. Worse is that the call of `draw()`
compiled at all: there was an implicit conversion from array to pointer (array
decay) and then another implicit conversion from `Circle` to `Shape`. There is
no way that `draw()` can safely iterate through that array: it has no way of
knowing the size of the elements.

**Alternative.** Use a support class that ensures that the number of elements is
correct and prevents dangerous implicit conversions. For example:

```cpp
void draw2(span<Circle>);
Circle arr[10];
// ...
draw2(span<Circle>(arr));  // deduce the number of elements
draw2(arr);    // deduce the element type and array size

void draw3(span<Shape>);
draw3(arr);    // error: cannot convert Circle[10] to span<Shape>
```

This `draw2()` passes the same amount of information to `draw()`, but makes the
fact that it is supposed to be a range of `Circle`s explicit.

**Exception.** Use `zstring` and `czstring` to represent C-style,
zero-terminated strings. But when doing so, use `std::string_view` or
`span<char>` from the GSL to prevent range errors.

**Enforcement.**

- (Simple) ((Bounds)) Warn for any expression that would rely on implicit
  conversion of an array type to a pointer type. Allow exception for
  `zstring`/`czstring` pointer types.
- (Simple) ((Bounds)) Warn for any arithmetic operation on an expression of
  pointer type that results in a value of pointer type. Allow exception for
  `zstring`/`czstring` pointer types.

## Rationale

Adopted as written, with `std::span` for `gsl::span` and `std::string_view` for
`czstring` — both available under C++20 and both already vendored in the sense
that they are the standard library.

This is the rule with the largest gap between what the project has decided and
what the project does. `std::span` was included in the precompiled header,
which is a statement of intent, and then never used. Every buffer in `Fs` and
`Text` still travels as a pointer and a length, or as a container reference that
loses the ability to describe a sub-range.

The Win32 boundary is exempt for the reason
[0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
gives: `ReadDirectoryChangesW` takes a pointer and a length and cannot be asked
to do otherwise. What is not exempt is the code that walks the result afterwards.

## Consequences

### Positive

- A concrete, mechanical change with a real payoff: a `span` parameter cannot be
  called with a mismatched length.
- The two enforcement points are checkable by a bounds profile, so this is one
  of the few rules a future analyser could enforce outright.

### Negative

- Converting `Fs` and `Text` to spans is a broad, behaviour-neutral change —
  the kind that is easy to defer forever and risky to do in one pass, because
  nothing in the test suite would notice a subtly wrong sub-range.
- `std::span` is non-owning, so every conversion raises a lifetime question that
  a container reference did not.

### Neutral

- No array-to-pointer decay of the `draw(Shape*, int)` kind exists here; the
  problem in this codebase is buffers, not polymorphic arrays.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `std::span` included but never used | `Core/src/corepch.h:13` | The intent was recorded and not carried out |
| Buffer walked as pointer plus length after the Win32 call | `Core/src/Core/Fs/DirectoryWatcher.cpp` | The call itself is exempt; the walk over its result is the fixable part |

## Related

- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  previous rule.
- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — the next
  rule.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — P.6, which this implements at the interface.
