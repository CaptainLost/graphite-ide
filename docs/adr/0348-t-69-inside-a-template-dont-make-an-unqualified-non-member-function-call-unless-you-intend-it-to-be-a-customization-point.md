# 0348. T.69: Inside a template, don't make an unqualified non-member function call unless you intend it to be a customization point

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A template should provide only the flexibility it intends, and should not be
vulnerable to accidental changes in its environment. There are three major ways
to let calling code customize a template:

```cpp
template<class T>
    // Call a member function
void test1(T t)
{
    t.f();    // require T to provide f()
}

template<class T>
void test2(T t)
    // Call a non-member function without qualification
{
    f(t);     // require f(/*T*/) be available in caller's scope or in T's namespace
}

template<class T>
void test3(T t)
    // Invoke a "trait"
{
    test_traits<T>::f(t); // require customizing test_traits<>
                          // to get non-default functions/types
}
```

A trait is usually a type alias to compute a type, a `constexpr` function to
compute a value, or a traditional traits template to be specialized on the
user's type.

The middle form is the dangerous one when it is not deliberate. If you intend
to call your own helper function `helper(t)` with a value `t` whose type
depends on a template parameter, put it in a `detail` namespace and qualify the
call as `detail::helper(t)`. An unqualified call becomes a customization point
at which any function named `helper` in the namespace of `t`'s type can be
invoked, and that includes unconstrained function templates the author of the
template never heard of.

## Decision

Inside a template, decide for each non-member call whether it is a
customization point, and write it accordingly.

- Qualify calls to your own helpers — `detail::helper(t)` — and put those
  helpers in a `detail` namespace so that argument-dependent lookup cannot
  reach an unrelated overload.
- Leave a call unqualified only where a caller is meant to supply the function
  for their own type, and document that requirement as part of the interface.
- Where the customization point is deliberate, use the `using std::swap;`
  followed by an unqualified `swap(a, b)` form, so the fallback is named
  explicitly.
- Prefer a member call or a trait when the customization should be explicit
  rather than found by lookup.

## Rationale

An unqualified call in a template is an extension point whether or not you
wanted one: the set of candidates is decided by the argument's namespace, which
belongs to somebody else. The failure mode is not a compile error but a
silently different function, and unconstrained templates with common names make
that outcome likely rather than theoretical. Qualifying the call is the only way
to say "this one is mine" in a form the compiler enforces.

## Consequences

### Positive

- The set of functions a template can call is fixed by the template's author.
- Adding an overload in an unrelated namespace cannot change what an existing
  template does.
- The intended customization points become a short, documentable list rather
  than every unqualified call in the body.

### Negative

- Qualification costs a `detail` namespace and a longer name at every call, so
  template bodies get noisier.
- It is easy to over-apply: qualifying `swap`, `begin`, `end` or `size` breaks
  the standard customization protocol and silently loses a user's efficient
  implementation.
- The rule cannot be applied to code you did not write; a template you call may
  make unqualified calls of its own, so the hole is only closed locally.
- Deciding which calls should be customization points is design work that has
  to be redone whenever the template's requirements change.

### Neutral

- The three mechanisms — member, unqualified non-member, trait — are all
  legitimate; the rule is about choosing one on purpose.
- A `detail` namespace is a convention, not a protection; it discourages use
  without preventing it.

## Enforcement

- In a template, flag an unqualified call to a non-member function that passes
  a variable of dependent type when there is a non-member function of the same
  name in the template's own namespace.

## Related

- [0341](0341-t-60-minimize-a-templates-context-dependencies.md) — unqualified
  calls are the main way a template acquires a dependency on its context.
- [0130](0130-c-165-use-using-for-customization-points.md) — how to write the
  calls that are meant to be customization points.
- [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md)
  — the reason a type's namespace is full of functions that lookup will find.
