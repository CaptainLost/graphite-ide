# 0166. R.32: Take a unique_ptr<widget> parameter to express that a function assumes ownership of a widget

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Using `unique_ptr` in this way both documents and enforces the function call's
ownership transfer.

```cpp
void sink(unique_ptr<widget>); // takes ownership of the widget
void uses(widget*);            // just uses the widget
```

The two declarations differ in what the caller is left holding. After `sink`,
the caller's `unique_ptr` is empty, because a `unique_ptr` cannot be copied and
the argument had to be moved in; the compiler enforces the transfer rather than
documenting it. After `uses`, the caller still owns the widget and nothing has
changed.

The form to avoid:

```cpp
void thinko(const unique_ptr<widget>&); // usually not what you want
```

A reference to a `const unique_ptr` cannot take ownership, cannot reseat, and
cannot outlive the call. It therefore expresses nothing about lifetime at all,
while restricting callers to those who happen to hold the widget in a
`unique_ptr`. If the function only uses the widget, it should take `widget&`, or
`widget*` if absence is possible.

## Decision

Take `unique_ptr<widget>` by value when, and only when, the function assumes
ownership.

- Declare the parameter as `std::unique_ptr<widget>` by value; callers pass with
  `std::move`, which is where the transfer becomes visible at the call site.
- Move the parameter into wherever the widget is going to live. A `sink`
  function that neither stores nor destroys the widget has taken ownership for
  no reason.
- Never take `const std::unique_ptr<widget>&`. Take `widget&` or `widget*` if
  the function merely uses the object.
- Do not take `unique_ptr<widget>&` unless the function reseats the caller's
  pointer, which is a different statement.

## Rationale

This is the rare case where the documentation and the enforcement are the same
construct. The signature says ownership moves, and because `unique_ptr` is
move-only, ownership does move: the caller cannot keep a copy, cannot forget to
transfer, and is left with an empty pointer that will not double-delete. A raw
pointer with a comment saying "takes ownership" makes the same claim and
enforces none of it.

## Consequences

### Positive

- Ownership transfer is stated in the signature and guaranteed by the type
  system.
- The call site shows the transfer, because `std::move` has to be written.
- The caller is left with an empty pointer, so a stale use is a null dereference
  rather than a use-after-free.

### Negative

- The parameter type dictates the caller's ownership scheme: a caller holding
  the widget on the stack, in a container, as a member, or in a `shared_ptr`
  cannot call the function at all without allocating a copy.
- It therefore pushes objects onto the free store purely to satisfy an
  interface, against the preference for scoped objects.
- Ownership transfer is all-or-nothing and irreversible from the caller's side;
  if the function fails part way, handing the widget back requires a channel the
  signature does not provide — a `unique_ptr` return, or an out parameter — and
  otherwise the object is destroyed with the failure.
- The deleter is part of the type, so `unique_ptr<widget, D>` will not bind, and
  a function that wants to accept any exclusively-owned widget must be a
  template.

### Neutral

- Whether a widget should be heap-allocated in the first place is decided
  elsewhere.
- The same reasoning applies to a constructor that takes ownership of its
  argument.

## Enforcement

- (Simple) Warn if a function takes a unique pointer parameter by lvalue
  reference and does not either assign to it or call `reset()` on it on at least
  one code path. Suggest taking a `T*` or `T&` instead.
- (Simple) Warn if a function takes a unique pointer parameter by reference to
  `const`. Suggest taking a `const T*` or `const T&` instead.

## Related

- [0167](0167-r-33-take-a-unique-ptr-widget-parameter-to-express-that-a-function-reseats-thewidget.md)
  — the other `unique_ptr` parameter form, and what it means instead.
- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — the rule that says when any of this is warranted.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the alternative this replaces.
- [0160](0160-r-21-prefer-unique-ptr-over-shared-ptr-unless-you-need-to-share-ownership.md)
  — why the parameter is a `unique_ptr` and not a `shared_ptr`.
