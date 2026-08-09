# 0046. F.7: For general use, take T* or T& arguments rather than smart pointers

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

No function in this repository takes a `unique_ptr` or `shared_ptr` parameter;
`shared_ptr` is not used at all. `LayerStack` and `PanelManager` own their
elements by `unique_ptr` and hand out `Layer*` and `Panel*`, which is the shape
this rule asks for.

## Decision

Adopt F.7 as written. Transcribed in full:

> **Reason.** Passing a smart pointer transfers or shares ownership and should
> only be used when ownership semantics are intended. A function that does not
> manipulate lifetime should take raw pointers or references instead.
>
> Passing by smart pointer restricts the use of a function to callers that use
> smart pointers. A function that needs a widget should be able to accept any
> widget object, not just ones whose lifetimes are managed by a particular kind
> of smart pointer.
>
> Passing a shared smart pointer (e.g., `std::shared_ptr`) implies a run-time
> cost.

**Example.**

```cpp
// accepts any int*
void f(int*);

// can only accept ints for which you want to transfer ownership
void g(unique_ptr<int>);

// can only accept ints for which you are willing to share ownership
void g(shared_ptr<int>);

// doesn't change ownership, but requires a particular ownership of the caller
void h(const unique_ptr<int>&);

// accepts any int
void h(int&);
```

**Example, bad.**

```cpp
// callee
void f(shared_ptr<widget>& w)
{
    // ...
    use(*w); // only use of w -- the lifetime is not used at all
    // ...
};

// caller
shared_ptr<widget> my_widget = /* ... */;
f(my_widget);

widget stack_widget;
f(stack_widget); // error
```

**Example, good.**

```cpp
// callee
void f(widget& w)
{
    // ...
    use(w);
    // ...
};

// caller
shared_ptr<widget> my_widget = /* ... */;
f(*my_widget);

widget stack_widget;
f(stack_widget); // ok -- now this works
```

**Note.** We can catch many common cases of dangling pointers statically (see
lifetime safety profile). Function arguments naturally live for the lifetime of
the function call, and so have fewer lifetime problems.

**Enforcement.**

- (Simple) Warn if a function takes a parameter of a smart pointer type (that
  overloads `operator->` or `operator*`) that is copyable but the function only
  calls any of: `operator*`, `operator->` or `get()`. Suggest using a `T*` or
  `T&` instead.
- Flag a parameter of a smart pointer type (a type that overloads `operator->`
  or `operator*`) that is copyable/movable but never copied/moved from in the
  function body, and that is never modified, and that is not passed along to
  another function that could do so. That means the ownership semantics are not
  used. Suggest using a `T*` or `T&` instead.

## Rationale

Adopted as written and already satisfied. Recording it matters because the
temptation runs the other way: `LayerStack` and `PanelManager` hold
`unique_ptr`s, and it is easy to pass what you hold rather than what the callee
needs.

The rule composes with [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md):
between them, a parameter should be a reference when the object is required, a
raw pointer when it is optional, and a smart pointer only when ownership moves.
That is a complete decision procedure for parameter types in this codebase.

## Consequences

### Positive

- Keeps functions callable with stack objects, which is what makes the `Core`
  test suite able to construct things directly rather than through a factory.
- Together with I.12 it leaves no ambiguity about what a parameter type means.

### Negative

- Passing `Layer*` obtained from a `unique_ptr` separates the pointer from the
  guarantee that the owner outlives the call. The rule's Note says arguments
  naturally live for the call, which is true for the parameter and not for what
  the callee stores.

### Neutral

- The `shared_ptr` half has no subject here; shared ownership is not used
  anywhere.

## Related

- [0045](0045-f-6-if-your-function-must-not-throw-declare-it-noexcept.md) — the
  previous rule.
- [0047](0047-f-8-prefer-pure-functions.md) — the next rule.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  other half of the parameter-type procedure.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  what a smart pointer parameter would mean.
