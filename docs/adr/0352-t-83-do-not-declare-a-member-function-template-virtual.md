# 0352. T.83: Do not declare a member function template virtual

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

C++ does not support it. If it did, vtables could not be generated until link
time, since a new instantiation may appear in any translation unit and would
have to add a slot; and implementations must in general deal with dynamic
linking, where translation units arrive after the program has started.

```cpp
class Shape {
    // ...
    template<class T>
    virtual bool intersect(T* p);   // error: template cannot be virtual
};
```

The guidelines keep this rule not because the mistake survives compilation, but
because people keep asking for it.

The alternatives, when a call really does have to dispatch on more than one
type, are double dispatch, the visitor pattern, or computing which function to
call and then calling it through an ordinary virtual function or a lookup
table.

## Decision

Do not attempt to combine a template parameter with virtual dispatch on the
same member function.

- Do not write `virtual` on a member function template; it will not compile.
- Where dispatch on a second type is needed, use double dispatch: a virtual
  call on the first argument, then a virtual call on the second from inside it.
- Where the set of types is fixed and known, use a visitor, or `std::variant`
  with `std::visit`, so the compiler checks that every case is handled.
- Where a small set of instantiations is needed polymorphically, declare a
  non-template virtual function per concrete parameter type instead.

## Rationale

The requirement that a class's vtable be complete when the class is compiled is
what makes virtual dispatch a constant-cost operation, and a member function
template has no fixed set of instantiations until the whole program is known.
The rule is not a style preference but a consequence of separate compilation
and dynamic linking, which is why no compiler will be persuaded.

## Consequences

### Positive

- Time is not spent trying to make a construct work that the language does not
  have.
- The design conversation moves to the mechanisms that do work — double
  dispatch, visitors, explicit dispatch tables — which are all expressible
  today.

### Negative

- The alternatives are considerably more code. Double dispatch requires each
  type to know about the other side of the dispatch, and a visitor closes the
  type set, so adding a type means editing the visitor.
- Upstream only names the alternatives; it gives no worked example, so the
  record cannot show what the replacement looks like.
- The rule adds nothing a compiler does not already say, so its value is
  confined to design discussions.

### Neutral

- A class template may have virtual functions, and a non-template member of a
  class template may be virtual; only the member function template itself is
  excluded.
- Enforcement is not a project concern here.

## Enforcement

- The compiler handles it.

## Related

- [0349](0349-t-80-do-not-naively-templatize-a-class-hierarchy.md) — the other
  boundary between templates and hierarchies, this one about cost rather than
  legality.
- [0107](0107-c-132-dont-make-a-function-virtual-without-reason.md) — before
  reaching for double dispatch, check the dispatch is needed.
- [0124](0124-c-153-prefer-virtual-function-to-casting.md) — why the workaround
  should stay within the dispatch mechanisms rather than becoming a chain of
  casts.
