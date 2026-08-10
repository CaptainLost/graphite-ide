# 0032. I.13: Do not pass an array as a single pointer

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

`(pointer, size)`-style interfaces are error-prone, and a plain pointer to an
array must rely on some convention to allow the callee to determine the size.

```cpp
void copy_n(const T* p, T* q, int n); // copy from [p:p+n) to [q:q+n)
```

What if there are fewer than `n` elements in the array pointed to by `q`? Then
some probably unrelated memory is overwritten. What if there are fewer than `n`
in the array pointed to by `p`? Then some probably unrelated memory is read.
Either is undefined behaviour and a potentially very nasty bug. Explicit spans
say it instead:

```cpp
void copy(span<const T> r, span<T> r2); // copy r to r2
```

The second example is worse than it looks:

```cpp
void draw(Shape* p, int n);  // poor interface; poor code
Circle arr[10];
// ...
draw(arr, 10);
```

Passing `10` as `n` might be a mistake: the most common convention is to assume
`[0:n)`, but that is nowhere stated. Worse is that the call compiled at all —
there was an implicit conversion from array to pointer, array decay, and then
another from `Circle` to `Shape`. There is no way `draw()` can safely iterate
through that array: it has no way of knowing the size of the elements.

A support class ensures the number of elements is correct and prevents the
dangerous implicit conversion:

```cpp
void draw2(span<Circle>);
Circle arr[10];
draw2(span<Circle>(arr));  // deduce the number of elements
draw2(arr);                // deduce the element type and array size

void draw3(span<Shape>);
draw3(arr);                // error: cannot convert Circle[10] to span<Shape>
```

`draw2()` passes the same amount of information, but makes explicit that it is
supposed to be a range of `Circle`s.

The exception: use `zstring` and `czstring` to represent C-style, zero-terminated
strings — but when doing so, use `std::string_view` or `span<char>` to prevent
range errors.

## Decision

Do not pass an array as a single pointer, and do not pass one as a pointer and a
separate count.

- Pass a `span`, a `string_view`, or a container reference — one object that
  carries the extent.
- Let the range type be deduced from the array at the call site rather than
  writing the length by hand.
- Where a C-style zero-terminated string is meant, use `zstring` or `czstring`
  to say so, and prefer `string_view` or `span<char>` for anything that indexes
  into it.
- Retain a `(pointer, count)` signature only where an external ABI requires it,
  and convert to a range type immediately inside the boundary.

## Rationale

This is the concrete, checkable form of
[0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md).
The count passed separately is not merely redundant — it is a second source of
truth that can disagree with the first, and nothing in the language relates
them.

The element-slicing part of the `draw` example is the argument that a comment
cannot answer. `draw(arr, 10)` compiles, is wrong in two independent ways at
once, and no amount of care at the call site would have shown it. `span<Shape>`
rejects it.

## Consequences

### Positive

- The extent travels with the data, so it cannot be wrong.
- Array-to-pointer decay and the derived-to-base slicing it enables stop
  compiling.
- Call sites get shorter, because the length is deduced.

### Negative

- `span` is a view: it says nothing about lifetime, so a dangling `span` is as
  easy to produce as a dangling pointer, and looks safer.
- It does not survive a C ABI boundary, so the old form persists at exactly the
  boundaries that matter.
- Bounds are checkable, not necessarily checked; whether they are depends on the
  implementation and the build configuration.

### Neutral

- Zero-terminated strings are an explicitly permitted exception, with their own
  named types.
- This record concerns interfaces; how a range is stored internally is not its
  business.

## Enforcement

- (Simple) Warn for any expression that would rely on implicit conversion of an
  array type to a pointer type. Allow an exception for `zstring`/`czstring`
  pointer types.
- (Simple) Warn for any arithmetic operation on an expression of pointer type
  that results in a value of pointer type. Allow an exception for
  `zstring`/`czstring` pointer types.

## Related

- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — the philosophy rule, with the same examples.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — why
  `read(span<int>)` beats `read(int*, int)`.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — `copy_n(p, q, n)` is also an argument-order trap.
