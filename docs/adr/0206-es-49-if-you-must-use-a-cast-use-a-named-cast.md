# 0206. ES.49: If you must use a cast, use a named cast

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability, and error avoidance. Named casts are more specific than a C-style
or functional cast, which allows the compiler to catch some errors.

The named casts are `static_cast`, `const_cast`, `reinterpret_cast`,
`dynamic_cast`, `std::move` — `move(x)` is an rvalue reference to `x` —
`std::forward` — `forward<T>(x)` is an rvalue or an lvalue reference to `x`
depending on `T` — `gsl::narrow_cast`, which is `static_cast<T>(x)`, and
`gsl::narrow`, which is `static_cast<T>(x)` if that round-trips and throws
`narrowing_error` otherwise.

```cpp
class B { /* ... */ };
class D { /* ... */ };

template<typename D> D* upcast(B* pb)
{
    D* pd0 = pb;                        // error: no implicit conversion from B* to D*
    D* pd1 = (D*)pb;                    // legal, but what is done?
    D* pd2 = static_cast<D*>(pb);       // error: D is not derived from B
    D* pd3 = reinterpret_cast<D*>(pb);  // OK: on your head be it!
    D* pd4 = dynamic_cast<D*>(pb);      // OK: return nullptr
    // ...
}
```

The example was synthesised from real-world bugs where `D` used to be derived
from `B`, but someone refactored the hierarchy. The C-style cast is dangerous
precisely because it can do any kind of conversion, depriving the code of
protection from mistakes, now or in the future.

When converting between types with no information loss — `float` to `double`, or
`int32` to `int64` — brace initialisation may be used instead:

```cpp
double d {some_float};
int64_t i {some_int32};
```

This makes it clear that the type conversion was intended, and it also prevents
conversions that might lose precision. It is a compilation error to initialise a
`float` from a `double` this way.

`reinterpret_cast` can be essential, but the essential uses — turning a machine
address into a pointer, for instance — are not type safe:

```cpp
auto p = reinterpret_cast<Device_register>(0x800);  // inherently dangerous
```

## Decision

When a conversion is unavoidable, name the conversion you mean and no other.

- Use `static_cast` for related types and well-defined arithmetic conversions,
  `dynamic_cast` for a downcast that must be checked, `const_cast` only under
  [0207](0207-es-50-dont-cast-away-const.md)'s exception, and
  `reinterpret_cast` only where nothing else can express the operation.
- Never use a C-style cast `(T)e` or a functional cast `T(e)`. Each of them
  performs whichever of `const_cast`, `static_cast`, `reinterpret_cast` and
  their combinations happens to be viable, without saying which — so the same
  syntax means something different after the types it was written against
  change.
- Where the conversion is lossless, use brace initialisation `T{e}` rather than
  any cast, so that a future change making it lossy becomes a compile error.
- Where narrowing is intended, say so with a named narrowing operation rather
  than a bare `static_cast`; see
  [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md).
- Treat `std::move` and `std::forward` as the casts they are, and apply
  [0209](0209-es-56-write-std-move-only-when-you-need-to-explicitly-move-an-object-to-another-scope.md)
  to their use.
- Confine `reinterpret_cast` to a named wrapper function at the boundary where
  the untyped thing enters, so that the unsafe operation appears once.

## Rationale

The refactoring story is the argument. `(D*)pb` was correct when `D` derived
from `B`, and it stayed compiling — silently becoming a `reinterpret_cast` —
when the hierarchy changed. `static_cast<D*>(pb)` would have failed to compile
at exactly that moment. A named cast is a statement about what relationship is
believed to hold, and the compiler checks the statement.

The corollary is that the named casts differ enormously in what they promise.
Writing `reinterpret_cast` is not an improvement in safety over a C-style cast;
it is an improvement in visibility, which is what lets a reviewer or a grep find
the places where safety was given up.

## Consequences

### Positive

- The compiler verifies the specific relationship the author claims, rather than
  finding whatever conversion happens to work.
- The dangerous conversions become greppable, so an audit of `reinterpret_cast`
  and `const_cast` is a finite piece of work.
- Brace initialisation covers the lossless cases with no cast at all, which is
  the majority of them.

### Negative

- Named casts are verbose, and the verbosity is uniform: `static_cast<int>(x)`
  on a routine conversion is as loud as `reinterpret_cast` on a dangerous one,
  which flattens the signal the rule is trying to create.
- `static_cast` between arithmetic types still narrows silently, so the most
  common named cast in practice provides no protection at all — the type profile
  warns about it, which is an admission that the rule is not enough on its own.
- Listing `std::move` and `std::forward` among the casts is correct but
  counter-intuitive, and readers who do not internalise it write
  `std::move(some_const_object)` and get a silent copy.
- The upstream list includes two guidelines-support-library functions that are
  not in the standard library, so following the rule as written requires either
  that dependency or a local equivalent.

### Neutral

- `dynamic_cast` has a run-time cost and requires RTTI; where the hierarchy
  navigation itself is the problem, a virtual function is the better fix.

## Enforcement

- Flag all C-style casts, including casts to `void`.
- Flag functional-style casts using `Type(value)`; use `Type{value}` instead,
  which does not narrow.
- The type profile bans `reinterpret_cast`.
- The type profile warns when `static_cast` is used between arithmetic types.

## Related

- [0205](0205-es-48-avoid-casts.md) — the rule this one is the fallback for.
- [0207](0207-es-50-dont-cast-away-const.md) — the named cast with the sharpest
  restrictions.
- [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md)
  — narrowing needs its own named operation, not a plain `static_cast`.
- [0209](0209-es-56-write-std-move-only-when-you-need-to-explicitly-move-an-object-to-another-scope.md)
  — `std::move` is on the list of named casts.
