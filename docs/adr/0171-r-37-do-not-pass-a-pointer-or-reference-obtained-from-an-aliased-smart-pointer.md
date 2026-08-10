# 0171. R.37: Do not pass a pointer or reference obtained from an aliased smart pointer

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Violating this rule is the number one cause of losing reference counts and
finding yourself with a dangling pointer. Functions should prefer to pass raw
pointers and references down call chains; at the top of the call tree you obtain
the raw pointer or reference from a smart pointer that keeps the object alive.
For that to be safe, you need to be sure that the smart pointer cannot
inadvertently be reset or reassigned from within the call tree below.

```cpp
// global (static or heap), or aliased local ...
shared_ptr<widget> g_p = ...;
void f(widget& w)
{
    g();
    use(w);  // A
}
void g()
{
    g_p = ...; // oops, if this was the last shared_ptr to that widget,
               // destroys the widget
}
```

The following should not pass code review:

```cpp
void my_code()
{
    // BAD: passing pointer or reference obtained from a non-local smart pointer
    //      that could be inadvertently reset somewhere inside f or its callees
    f(*g_p);
    // BAD: same reason, just passing it as a "this" pointer
    g_p->func();
}
```

The fix is to take a local copy of the pointer, to keep a reference count for
the duration of your call tree:

```cpp
void my_code()
{
    // cheap: 1 increment covers this entire function and all the call trees
    // below us
    auto pin = g_p;
    // GOOD: passing pointer or reference obtained from a local unaliased smart
    // pointer
    f(*pin);
    // GOOD: same reason
    pin->func();
}
```

Note that `g_p->func()` is caught by the same rule as `f(*g_p)`. The implicit
`this` pointer is a raw pointer obtained from an aliased smart pointer just as
surely as the explicit dereference is, and a member function that reaches the
same global is the easiest way to reproduce the bug.

## Decision

Never derive a raw pointer or reference from a smart pointer that something else
could reset during the call.

- Before using a non-local smart pointer — a global, a static, a member
  reachable from elsewhere, or a local that has been aliased — copy it into a
  local variable, and derive the pointer or reference from that copy.
- Treat a member function call through such a pointer as a use of the object;
  pin first, then call.
- Keep the pinning copy alive for the whole call tree, not merely the first
  call.
- Pass raw pointers and references down the call chain as usual; the pinning
  belongs at the top of the tree, not at every level.
- Derive from `weak_ptr` only through `lock()`, holding the resulting
  `shared_ptr` for the duration of the use.

## Rationale

The hazard is that the count and the pointer become separated: the raw pointer
is what is passed, and the thing keeping the object alive is somewhere else
entirely, free to be reassigned by code that has no idea anyone is holding a
reference. The remedy is unusually cheap for the size of the bug — one atomic
increment covering an entire call tree — and it is local, requiring nothing of
the functions being called. That asymmetry is why the rule is worth applying
mechanically rather than case by case.

## Consequences

### Positive

- A single local copy makes an entire call tree safe against reseating,
  regardless of what the callees do.
- Callees keep taking plain references and pointers, so the interfaces below
  stay free of smart pointers.
- The rule gives review a concrete thing to look for: a dereference of a
  non-local smart pointer at a call site.

### Negative

- "Potentially aliased" is undecidable in general, so applying the rule honestly
  means pinning whenever aliasing cannot be ruled out, which is often, and the
  enforcement check must be correspondingly conservative.
- The pin costs an atomic increment and decrement and is trivially forgotten;
  omitting it produces code that works until the day some callee reseats, so
  testing does not find it.
- It applies only to `shared_ptr` as a fix. A raw pointer derived from an
  aliased `unique_ptr` cannot be pinned by copying, so there the only remedy is
  to restructure the ownership.
- Pinning keeps the object alive but does not protect any invariant the callee
  relies on; a widget mutated by a reseating call is still the wrong widget,
  just not a destroyed one.

### Neutral

- The rule constrains callers, not callees, which is what makes it applicable to
  code that calls libraries.
- Not having a non-local smart pointer in the first place removes the problem
  entirely.

## Enforcement

- (Simple) Warn if a pointer or reference obtained from a smart pointer variable
  that is non-local, or that is local but potentially aliased, is used in a
  function call. If the smart pointer is a shared pointer, suggest taking a
  local copy of it and obtaining the pointer or reference from that instead.

## Related

- [0152](0152-r-6-avoid-non-const-global-variables.md) — the mutable global
  smart pointer that makes this failure available.
- [0169](0169-r-35-take-a-shared-ptr-widget-parameter-to-express-that-a-function-might-reseat-the-shared-pointer.md)
  — a function declared to reseat is the callee half of this bug.
- [0163](0163-r-24-use-std-weak-ptr-to-break-cycles-of-shared-ptrs.md) —
  `lock()` is the same pin, applied to a weak reference.
- [0164](0164-r-30-take-smart-pointers-as-parameters-only-to-explicitly-express-lifetime-semantics.md)
  — the rule that sends raw pointers down call chains, and therefore needs this
  one.
