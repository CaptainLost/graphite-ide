# 0204. ES.47: Use nullptr rather than 0 or NULL

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Readability, and minimising surprises: `nullptr` cannot be confused with an
`int`. It also has a well-specified and very restrictive type, so it works in
more scenarios where type deduction might do the wrong thing with `NULL` or `0`.

```cpp
void f(int);
void f(char*);

f(0);         // call f(int)
f(nullptr);   // call f(char*)
```

`NULL` is a macro whose definition is implementation-defined; in C++ it is
typically `0` or `0L`, so it carries no more type information than the literal
does. `nullptr` has type `std::nullptr_t`, which converts to any pointer type
and to no integer type.

## Decision

Write `nullptr` wherever a null pointer value is meant.

- Use `nullptr` for null pointer arguments, comparisons, initialisers and return
  values.
- Do not use `0` or `NULL` as a pointer value, including in comparisons such as
  `if (p == 0)`.
- Do not use `nullptr` as an integer; it is not one.
- Where a template or `auto` deduces from a null argument, pass `nullptr` so
  that the deduced type is `std::nullptr_t` rather than `int`.
- Prefer removing the null case altogether where the interface allows it — a
  reference, or a not-null wrapper per
  [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md), says
  more than any spelling of null.

## Rationale

The overload example is the whole argument in three lines: `f(0)` and
`f(nullptr)` call different functions, and the reader who wrote `f(0)` meaning
"no pointer" got an integer conversion instead, with no diagnostic. Everything
else about the rule is stylistic; that part is a real defect.

The second-order benefit is deduction. `NULL` and `0` deduce to an integral
type, so a forwarding wrapper, a variadic call or an `auto` variable initialised
from them silently stops being about pointers at all.

## Consequences

### Positive

- Overload resolution and template deduction pick the pointer case, which is
  what the author meant.
- `nullptr` is greppable in a way that `0` is not, so pointer-related code can
  actually be found.
- Variadic C interfaces that require a null pointer sentinel get one of the
  right width, rather than an `int` that happens to be narrower than a pointer
  on some ABIs.

### Negative

- This is the cheapest of the pointer rules and the least valuable: it makes
  null pointers spell correctly, and does nothing about dereferencing one, which
  is the actual failure. See
  [0215](0215-es-65-dont-dereference-an-invalid-pointer.md).
- `NULL` continues to arrive from C headers and third-party APIs, so it will be
  present regardless, and a blanket ban has to carve out code that is not yours
  to change.
- A mechanical `0` to `nullptr` replacement can change program behaviour where
  the `0` was genuinely selecting an integer overload, so the migration is not
  purely textual.
- `0` remains a valid null pointer constant, so nothing in the language enforces
  the rule; it is a convention that has to be maintained by a checker or by
  review, forever, over code that compiles either way.

### Neutral

- `nullptr` has been available since C++11, so there is no portability argument
  left for the alternatives in any current dialect of C++.
- The rule concerns how a null pointer value is spelled, not whether a pointer
  should be nullable at all.

## Enforcement

Flag uses of `0` and `NULL` for pointers. The transformation might be helped by
simple program transformation.

## Related

- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  better answer where null is not a legal value at all.
- [0215](0215-es-65-dont-dereference-an-invalid-pointer.md) — the rule that
  addresses what actually goes wrong with null.
- [0009](0009-p-3-express-intent.md) — `nullptr` says "pointer"; `0` says
  nothing.
