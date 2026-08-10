# 0450. F.18: For "will-move-from" parameters, pass by X&& and std::move the parameter

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

It is efficient, and it eliminates bugs at the call site: `X&&` binds to
rvalues, which requires an explicit `std::move` at the call site if an lvalue
is being passed.

```cpp
void sink(vector<int>&& v)  // sink takes ownership of whatever the argument owned
{
    // usually there might be const accesses of v here
    store_somewhere(std::move(v));
    // usually no more use of v here; it is moved-from
}
```

Note that the `std::move(v)` makes it possible for `store_somewhere()` to leave
`v` in a moved-from state, which can be dangerous if anything later in the body
reads it.

There is an exception. Unique owner types that are move-only and cheap to move,
such as `unique_ptr`, can also be passed by value, which is simpler to write and
achieves the same effect. Passing by value generates one extra, cheap, move
operation, but simplicity and clarity come first:

```cpp
template<class T>
void sink(std::unique_ptr<T> p)
{
    // use p ... possibly std::move(p) onward somewhere else
}   // p gets destroyed
```

## Decision

When a function will unconditionally take the value out of its argument, take
the parameter by `X&&` and `std::move` it in the body.

- Use `X&&` where `X` is a concrete type, not a template parameter; a template
  parameter's `&&` is a forwarding reference and a different rule governs it.
- `std::move` the parameter exactly once, on every path, and do not read it
  afterwards.
- Do not move conditionally: a parameter moved on one branch and used on another
  leaves the caller unable to know the state of its object.
- Prefer passing by value for move-only, cheap-to-move types such as
  `unique_ptr`, and accept the one extra move.
- Do not use `X&&` for a parameter that is merely input; that is the ordinary
  "in" case.

## Rationale

The call-site effect is the real argument, not the saved copy. Because `X&&`
does not bind to an lvalue, a caller passing a named object must write
`std::move(x)`, and that word appears at the place where the loss of the value
actually matters — in the caller's own scope, where the variable will still be
visible on the following lines. A by-value parameter would accept the lvalue
silently and copy it; `X&&` refuses to let ownership move without saying so.

## Consequences

### Positive

- Ownership transfer is visible at the call site, spelled by the caller rather
  than buried in the callee.
- The copy is eliminated rather than merely optimised away, so the guarantee
  does not depend on the compiler.
- A caller who did not intend to give up the object gets a compile error, not a
  silent copy or a silent move.

### Negative

- The parameter is left moved-from, and the language does not stop later lines
  of the body, or the caller, from reading it; this is the failure mode the rule
  itself flags twice.
- `X&&` and `TP&&` look identical but mean opposite things, and confusing them
  is one of the easiest mistakes in modern C++.
- Supporting both lvalue and rvalue callers now needs two overloads, doubling
  the surface where one by-value parameter would have done.
- Every call site gets noisier, and `std::move` on the wrong expression silently
  degrades to a copy.

### Neutral

- The by-value alternative for `unique_ptr` costs one move and is explicitly
  endorsed; the two forms are interchangeable there.
- This rule is one of the advanced parameter-passing techniques, so it is
  expected to be used rarely and with a stated reason.

## Enforcement

- Flag all `X&&` parameters, where `X` is not a template type parameter name,
  that the function body uses without `std::move`.
- Flag access to moved-from objects.
- Flag conditional moves from an object.

## Related

- [0451](0451-f-19-for-forward-parameters-pass-by-tp-and-only-std-forward-the-parameter.md)
  — the forwarding reference this is constantly confused with.
- [0447](0447-f-15-prefer-simple-and-conventional-ways-of-passing-information.md)
  — the rule that classifies this as advanced and requires a stated need.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the transfer being expressed here, in its general form.
- [0444](0444-f-7-for-general-use-take-t-or-t-arguments-rather-than-smart-pointers.md)
  — a smart pointer parameter is right precisely in this case.
