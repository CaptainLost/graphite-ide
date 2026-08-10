# 0464. F.45: Don't return a T&&

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Returning a `T&&` is asking to return a reference to a destroyed temporary
object. A `&&` is a magnet for temporary objects, and a returned rvalue
reference goes out of scope at the end of the full expression to which it is
returned:

```cpp
auto&& x = max(0, 1);   // OK, so far
foo(x);                 // Undefined behavior
```

This kind of use is a frequent source of bugs, often incorrectly reported as a
compiler bug. An implementer of a function should avoid setting such traps for
users.

Returning an rvalue reference is fine when the reference to the temporary is
being passed "downward" to a callee; the temporary is then guaranteed to outlive
the function call. It is not fine when passing such a reference "upward" to a
larger caller scope. For passthrough functions that take parameters by ordinary
reference or by perfect forwarding and want to return values, use simple `auto`
return type deduction, not `auto&&`.

Assume that `F` returns by value:

```cpp
template<class F>
auto&& wrapper(F f)
{
    log_call(typeid(f)); // or whatever instrumentation
    return f();          // BAD: returns a reference to a temporary
}
```

Better:

```cpp
template<class F>
auto wrapper(F f)
{
    log_call(typeid(f)); // or whatever instrumentation
    return f();          // OK
}
```

The exception is that `std::move` and `std::forward` do return `&&`, but they
are just casts, used by convention only in expression contexts where a reference
to a temporary object is passed along within the same expression before the
temporary is destroyed. There are no other known good examples of returning
`&&`.

## Decision

Do not declare `T&&` or `auto&&` as a return type.

- Return by value from a function that produces an object, and rely on
  guaranteed copy elision and move semantics for the efficiency.
- Write `auto`, not `auto&&`, on a forwarding or instrumenting wrapper whose
  callee returns by value.
- Where a wrapper must preserve the exact value category of the callee's result,
  use `decltype(auto)` deliberately and accept that it returns a reference
  exactly when the callee did — do not reach for `auto&&` to get that effect.
- Reserve `&&` returns for casts in the `std::move` / `std::forward` mould,
  consumed within the same full expression.
- Do not bind `auto&&` to a call whose return type you do not control; lifetime
  extension does not apply through a returned reference.

## Rationale

The trap is asymmetric, and that is what makes it worth a rule. Passing a
temporary downward is safe because the caller's full expression brackets the
callee's entire lifetime; passing one upward is unsafe for the same reason
reversed, and nothing in the syntax distinguishes the two directions.

The cost of obeying the rule is nil. Since C++17's guaranteed copy elision, the
by-value version of `wrapper` produces the same object in the same storage as
the reference version claimed to avoid copying — so the `&&` return buys
undefined behaviour in exchange for nothing.

## Consequences

### Positive

- A whole family of dangling-reference bugs, and the mistaken compiler-bug
  reports that follow them, becomes impossible to write.
- By-value returns compose safely: the caller may store, move, or ignore the
  result without knowing how it was produced.
- `auto` on a wrapper is shorter and more readable than `auto&&`, so the safe
  form is also the plainer one.

### Negative

- The rule removes the only tool for writing a perfectly transparent wrapper
  with `auto`; a wrapper over a function that legitimately returns `T&` will
  decay it to a copy unless `decltype(auto)` is used, which reintroduces a
  reference return by a different spelling.
- `auto&&` remains correct and useful as a *variable* declaration, so the token
  is not simply banned, and the distinction between the two positions has to be
  taught rather than mechanically applied.
- Enforcement must whitelist `std::move` and `std::forward`, so any check is
  name-based rather than principled and will miss local re-implementations of
  the same casts.
- Some hand-written expression-template and proxy libraries do return `&&`
  deliberately; the rule declares those wrong without offering a replacement
  idiom.

### Neutral

- The lifetime safety profile would catch these problems when completely
  implemented; this record does not assume it is available.
- The rule concerns the declared return type only. What a function does with
  `&&` parameters internally is the business of the forwarding rules.

## Enforcement

- Flag any use of `&&` as a return type, except in `std::move` and
  `std::forward`.

## Related

- [0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md)
  — the same dangling problem, in its non-rvalue form.
- [0463](0463-f-44-return-a-t-when-copy-is-undesirable-and-returning-no-object-isnt-needed.md)
  — the reference return that is legitimate.
- [0467](0467-f-48-dont-return-std-move-local.md) — the other case where an
  attempt to avoid a copy costs more than the copy.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — a
  returned `&&` is a type-safety hole the type system does not close.
