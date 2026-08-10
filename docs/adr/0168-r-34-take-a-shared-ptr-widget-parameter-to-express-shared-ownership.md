# 0168. R.34: Take a shared_ptr<widget> parameter to express shared ownership

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Taking a `shared_ptr` by value makes the function's ownership sharing explicit.

```cpp
class WidgetUser
{
public:
    // WidgetUser will share ownership of the widget
    explicit WidgetUser(std::shared_ptr<widget> w) noexcept:
        m_widget{std::move(w)} {}
    // ...
private:
    std::shared_ptr<widget> m_widget;
};
```

Two details of the example carry the rule. The parameter is taken by value,
which is what makes the sharing a fact rather than a claim: by the time the body
runs, the parameter holds a share of its own — copied from a caller's lvalue, or
transferred from a temporary or a `std::move` — so the widget cannot die while
this object holds it. And the parameter is moved into the member, so the
by-value form costs at most one increment for the whole transaction rather than
one per hop, and a caller who moves pays none.

The form is only correct when the function really does retain the object beyond
the call. A function that takes a `shared_ptr` by value and never copies or
moves it anywhere is paying for shared ownership it does not use, and should
have taken `widget&`.

## Decision

Take `std::shared_ptr<widget>` by value when the function or object becomes an
owner.

- Use it for constructors and functions that store the pointer in a member, a
  container, a queued task, or anything else outliving the call.
- Move the parameter into its destination rather than copying it, and let
  callers move at the call site where they have no further use for it.
- Do not take a `shared_ptr` by value in a function that only uses the widget;
  take `widget&`, or `widget*` if absence is possible.
- Do not take a `shared_ptr` by rvalue reference; take it by value and move.
- Take `shared_ptr<widget>&` only to reseat, and `const shared_ptr<widget>&`
  only where a reference count might be retained on some path.

## Rationale

By-value is what distinguishes a promise from a mechanism. Any of the reference
forms can be given a comment saying the function shares ownership; only the
by-value parameter already holds a share of its own by the time the body runs,
so the widget is alive for as long as the new owner needs it regardless of what
the caller does next — including reseating or destroying its own pointer. The
move into the member then makes the correct form also the cheap one.

## Consequences

### Positive

- Shared ownership is established by the call itself, not by a convention about
  what the body will do.
- Combined with a move into the destination, the cost is one refcount operation
  for the whole transfer, and none when the caller moves.
- The signature tells a reader that this object's lifetime is now partly this
  function's business.

### Negative

- The parameter type forces the caller into shared ownership: a widget owned by
  a `unique_ptr`, a member, or a stack frame cannot be passed, so the interface
  propagates `shared_ptr` outward through the codebase against the preference
  for exclusive ownership.
- A by-value `shared_ptr` on a hot path costs an atomic increment and decrement
  per call whenever the caller cannot move.
- Shared ownership makes the destruction point dynamic, so accepting it in an
  interface gives away the ability to say when the widget dies — and where it
  dies, since the last owner to release runs the destructor.
- The rule cannot distinguish "shares ownership" from "happens to copy the
  pointer somewhere", so the enforcement check passes any function that copies
  the argument on one path.

### Neutral

- Whether the widget should be shared-owned at all is decided by another rule;
  this one says how to express it once decided.
- The same reasoning applies to a member function that stores the pointer as to
  a constructor.

## Enforcement

- (Simple) Warn if a function takes a shared pointer parameter by lvalue
  reference and does not either assign to it or call `reset()` on it on at least
  one code path. Suggest taking a `T*` or `T&` instead.
- (Simple) Warn if a function takes a shared pointer by value or by reference to
  `const` and does not copy or move it to another shared pointer on at least one
  code path. Suggest taking a `T*` or `T&` instead.
- (Simple) Warn if a function takes a shared pointer by rvalue reference.
  Suggest taking it by value instead.

## Related

- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — the rule that limits when this form is warranted.
- [0169](0169-r-35-take-a-shared-ptr-widget-parameter-to-express-that-a-function-might-reseat-the-shared-pointer.md)
  and
  [0170](0170-r-36-take-a-const-shared-ptr-widget-parameter-to-express-that-it-might-retain-a-reference-count-to-the-object.md)
  — the other two `shared_ptr` parameter forms.
- [0160](0160-r-21-prefer-unique-ptr-over-shared-ptr-unless-you-need-to-share-ownership.md)
  — why this form should be rarer than it usually is.
- [0165](0165-r-31-if-you-have-non-std-smart-pointers-follow-the-basic-pattern-from-std.md)
  — the same rule applied to third-party shared pointers.
