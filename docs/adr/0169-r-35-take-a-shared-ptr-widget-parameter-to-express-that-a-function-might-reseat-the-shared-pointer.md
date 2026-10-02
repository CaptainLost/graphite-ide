# 0169. R.35: Take a shared_ptr<widget>& parameter to express that a function might reseat the shared pointer

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Taking a `shared_ptr` by non-`const` reference makes the function's reseating
explicit. "Reseat" means making a reference or a smart pointer refer to a
different object.

```cpp
void ChangeWidget(std::shared_ptr<widget>& w)
{
    // This will change the callers widget
    w = std::make_shared<widget>(widget{});
}
```

The assignment does two things. It makes the caller's pointer refer to a new
widget, and it releases the caller's share of the old one — which destroys the
old widget if that share was the last. The signature announces the first and
says nothing about the second, so a function of this shape can destroy an object
from underneath every other holder of a pointer or reference derived from that
`shared_ptr`.

That is why the form is reserved for functions that genuinely reseat. A function
that takes `shared_ptr<widget>&` merely to avoid a refcount bump is claiming
reseating semantics it does not have, and any caller reading the declaration
must now assume its pointer can change.

## Decision

Take `std::shared_ptr<widget>&` only when the function may make the caller's
pointer refer to a different object.

- Use it for functions that assign to the parameter or call `reset()` on it.
- Take `widget&` or `widget*` if the function only uses the widget, and
  `shared_ptr<widget>` by value if it becomes an owner.
- Do not use it as an optimisation to avoid copying a `shared_ptr`; the form
  means something, and the meaning is not "cheap".
- State what the pointer refers to after the call, including whether it may be
  empty.
- Assume that reseating may destroy the old widget, and do not hold a pointer or
  reference derived from that `shared_ptr` across the call.

## Rationale

The three `shared_ptr` parameter forms differ in what they do to the caller's
pointer, and only this one changes it. Reserving the non-`const` reference for
that case keeps the by-value form meaning "I become an owner" and keeps the
signature honest about the one operation that can destroy a shared object at a
moment the caller did not choose. Using it for performance would make every
declaration in the codebase ambiguous about the one thing it exists to say.

## Consequences

### Positive

- Reseating, the only shared-pointer operation with an effect on the caller's
  variable, is distinguishable from sharing and from use.
- The compiler requires the caller to hold a mutable `shared_ptr` lvalue, so the
  possibility is visible at the call site.
- The by-value form retains its meaning, because this form has taken the other
  one away from it.

### Negative

- The signature does not distinguish "will reseat" from "might reseat", so
  callers must assume the worst on every path.
- Reseating releases the caller's share, which can destroy the widget as a side
  effect of a call whose name says nothing about destruction.
- A non-`const` lvalue reference binds only to a named `shared_ptr`, so a caller
  holding a temporary or a `const` pointer must materialise a variable purely to
  make the call.
- It is routinely misused as a way of passing a `shared_ptr` cheaply, and
  nothing in the declaration distinguishes the misuse from the intended use; the
  enforcement check only asks whether some path assigns or resets.

### Neutral

- The equivalent form for exclusive ownership is a separate rule with the same
  shape.
- Whether the widget should be shared at all is not this rule's question.

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

- [0167](0167-r-33-take-a-unique-ptr-widget-parameter-to-express-that-a-function-reseats-thewidget.md)
  — the same statement for exclusive ownership.
- [0168](0168-r-34-take-a-shared-ptr-widget-parameter-to-express-shared-ownership.md)
  — the by-value form this one must not be confused with.
- [0171](0171-r-37-do-not-pass-a-pointer-or-reference-obtained-from-an-aliased-smart-pointer.md)
  — the failure that a reseating function causes in its callers.
- [0170](0170-r-36-take-a-const-shared-ptr-widget-parameter-to-express-that-it-might-retain-a-reference-count-to-the-object.md)
  — the third form, for maybe-retaining without reseating.
