# 0444. F.7: For general use, take T* or T& arguments rather than smart pointers

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Passing a smart pointer transfers or shares ownership, and should only be done
when ownership semantics are intended. A function that does not manipulate
lifetime should take a raw pointer or a reference instead.

Passing by smart pointer also restricts the function to callers that use smart
pointers. A function that needs a widget should be able to accept any widget
object, not only ones whose lifetime is managed by a particular kind of smart
pointer. Passing a shared smart pointer such as `std::shared_ptr` implies a
run-time cost as well.

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

The bad case and the good case, side by side:

```cpp
// callee
void f(shared_ptr<widget>& w)   // bad
{
    // ...
    use(*w); // only use of w -- the lifetime is not used at all
    // ...
}

// caller
shared_ptr<widget> my_widget = /* ... */;
f(my_widget);
widget stack_widget;
f(stack_widget); // error
```

```cpp
// callee
void f(widget& w)   // good
{
    // ...
    use(w);
    // ...
}

// caller
shared_ptr<widget> my_widget = /* ... */;
f(*my_widget);
widget stack_widget;
f(stack_widget); // ok -- now this works
```

Many common cases of dangling pointers can be caught statically. Function
arguments naturally live for the duration of the function call, so they have
fewer lifetime problems than pointers stored elsewhere.

## Decision

Take a raw pointer or a reference unless the function participates in the
object's lifetime.

- If the body only dereferences the parameter, calls through it, or reads it,
  the parameter is `T&` or `T*`.
- Take a `unique_ptr<T>` by value only when the function takes over ownership,
  and a `shared_ptr<T>` by value only when it will retain a share of it.
- Never take `const unique_ptr<T>&` or `const shared_ptr<T>&` in order to "avoid
  a copy": it changes no ownership and needlessly excludes every caller who owns
  the object some other way.
- Dereference at the call site — `f(*my_widget)` — rather than widening the
  callee's parameter type to match the caller's storage.
- Choose between `T*` and `T&` by whether absence is a valid argument.

## Rationale

The parameter type decides who may call, and a smart pointer parameter makes
storage strategy part of the contract. `f(shared_ptr<widget>&)` cannot be called
with a stack widget, a member subobject, or a widget from a pool — none of which
have anything wrong with them — so the function has narrowed its applicability
in exchange for nothing, since it never touched the lifetime. A raw reference
says exactly what the function needs, which is a live widget for the duration of
the call, and that is a property every caller can supply.

## Consequences

### Positive

- The function becomes callable with objects held in any way: automatic storage,
  a member, an arena, a container element.
- The signature says truthfully whether lifetime is involved, so a smart pointer
  in a parameter list becomes informative.
- The atomic refcount traffic of passing `shared_ptr` around disappears from
  functions that never needed it.
- Argument lifetime is bounded by the call, which is the easiest case for both
  humans and lifetime analysis.

### Negative

- The callee gains no lifetime guarantee beyond the call, so storing the pointer
  is now a bug the type system does not prevent; the smart pointer version at
  least made retention safe.
- Call sites acquire dereference noise, and `f(*p)` is undefined if `p` is null,
  moving a check from the callee to every caller.
- Refactoring a function that later does need to retain the object means
  changing its signature and every call site.
- Where a caller genuinely wants the callee to extend a lifetime, this rule
  gives no way to express it, and the answer lives in a different record.

### Neutral

- The rule concerns parameters. What a function stores in a member, and how, is
  a separate question.
- `owner<T*>` remains available where a raw pointer must carry ownership across
  a boundary that cannot use smart pointers.

## Enforcement

- (Simple) Warn if a function takes a parameter of a smart pointer type — one
  that overloads `operator->` or `operator*` — that is copyable, but the
  function only calls `operator*`, `operator->` or `get()` on it. Suggest `T*`
  or `T&`.
- Flag a parameter of a smart pointer type that is copyable or movable but never
  copied or moved from in the body, never modified, and never passed on to
  another function that could do so. That means the ownership semantics are
  unused. Suggest `T*` or `T&` instead.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the complementary rule: a raw pointer parameter means no transfer.
- [0454](0454-f-60-prefer-t-over-t-when-no-argument-is-a-valid-option.md) — how
  to choose between the `T*` and `T&` this rule leaves open.
- [0450](0450-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
  — the shape to use when the function really does take ownership.
- [0455](0455-f-22-use-t-or-owner-t-to-designate-a-single-object.md) — what a
  plain `T*` parameter is thereby promising.
