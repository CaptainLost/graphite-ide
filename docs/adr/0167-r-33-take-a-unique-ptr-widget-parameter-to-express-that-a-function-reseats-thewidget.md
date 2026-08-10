# 0167. R.33: Take a unique_ptr<widget>& parameter to express that a function reseats the widget

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Using `unique_ptr` in this way both documents and enforces the function call's
reseating semantics. "Reseat" means making a pointer or a smart pointer refer to
a different object.

```cpp
void reseat(unique_ptr<widget>&); // "will" or "might" reseat pointer
```

The form to avoid is the same one the previous rule warns about:

```cpp
void thinko(const unique_ptr<widget>&); // usually not what you want
```

A `const unique_ptr<widget>&` cannot be reseated and cannot take ownership, so
it says nothing about lifetime while excluding every caller who owns the widget
by some other means.

The non-`const` reference is a strong statement, and it is easy to under-read.
It means the caller's pointer may point at a different object when the call
returns, and, because the old object is deleted at the moment of reassignment,
that the widget the caller had is very likely destroyed by the call. Any raw
pointer or reference the caller derived from that `unique_ptr` before the call
is dangling after it.

## Decision

Take `unique_ptr<widget>&` only when the function may make the caller's pointer
refer to a different object.

- Use it for functions that assign to the parameter or call `reset()` on it: a
  reload, a swap of implementation, a factory that replaces an existing object.
- Take `widget&` or `widget*` if the function only uses the widget, and
  `unique_ptr<widget>` by value if it takes ownership.
- Never take `const std::unique_ptr<widget>&`.
- Document what the pointer refers to after the call, including whether it can
  be left empty on a failure path.
- Do not hold a raw pointer or reference obtained from the same `unique_ptr`
  across such a call.

## Rationale

The distinction between this form and by-value ownership is which side of the
call holds the pointer afterwards, and the type says it exactly: by value, the
caller's pointer is emptied and the callee owns; by non-`const` reference, the
caller keeps the pointer and the callee decides what it points at. Both are
lifetime statements and neither is a way to pass a widget. The reason the rule
insists on the distinction is that reseating destroys the old object as a side
effect, which is a much bigger event than the signature's single `&` suggests.

## Consequences

### Positive

- The one situation in which a caller's owning pointer legitimately changes is
  spelled distinctly from the two situations in which it does not.
- The compiler enforces that the caller has a mutable `unique_ptr`, so the
  possibility of reseating is visible at the call site.
- It removes the temptation of `const unique_ptr<widget>&`, which is a
  pessimisation with no meaning.

### Negative

- This is an out-parameter, so the caller's state changes as a side effect of a
  call, which is exactly the shape of interface that is hardest to reason about.
- The destruction of the old widget is implicit in the reassignment and happens
  at a point the caller did not write, invalidating any borrowed pointer.
- "Will reseat" and "might reseat" are the same signature, so the caller cannot
  tell from the declaration whether the pointer is guaranteed to have changed.
- The enforcement check is a heuristic — assign or `reset()` on at least one
  path — so a function that reseats only through a helper will be flagged, and
  one that assigns pointlessly will pass.

### Neutral

- The parameter is a lifetime statement, not a way of passing the widget itself.
- The equivalent form for shared ownership is a separate rule with the same
  shape.

## Enforcement

- (Simple) Warn if a function takes a unique pointer parameter by lvalue
  reference and does not either assign to it or call `reset()` on it on at least
  one code path. Suggest taking a `T*` or `T&` instead.
- (Simple) Warn if a function takes a unique pointer parameter by reference to
  `const`. Suggest taking a `const T*` or `const T&` instead.

## Related

- [0166](0166-r-32-take-a-unique-ptr-widget-parameter-to-express-that-a-function-assumes-ownership-of-a-widget.md)
  — the by-value form, and the other half of this pair.
- [0169](0169-r-35-take-a-shared-ptr-widget-parameter-to-express-that-a-function-might-reseat-the-shared-pointer.md)
  — the same statement for shared ownership.
- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — the rule that limits when a smart pointer parameter is appropriate at all.
- [0171](0171-r-37-do-not-pass-a-pointer-or-reference-obtained-from-an-aliased-smart-pointer.md)
  — reseating is precisely what makes a borrowed pointer dangle.
