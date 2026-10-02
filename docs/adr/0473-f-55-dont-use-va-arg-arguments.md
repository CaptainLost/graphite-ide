# 0473. F.55: Don't use va_arg arguments

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Reading from a `va_arg` assumes that the correct type was actually passed.
Passing to varargs assumes the correct type will be read. This is fragile,
because it cannot generally be enforced to be safe in the language, and so
relies on programmer discipline to get it right.

```cpp
int sum(...)
{
    // ...
    while (/*...*/)
        result += va_arg(list, int); // BAD, assumes it will be passed ints
    // ...
}

sum(3, 2);              // ok
sum(3.14159, 2.71828);  // BAD, undefined
```

```cpp
template<class ...Args>
auto sum(Args... args) // GOOD, and much more flexible
{
    return (... + args); // note: C++17 "fold expression"
}

sum(3, 2);              // ok: 5
sum(3.14159, 2.71828);  // ok: ~5.85987
```

The alternatives are overloading, variadic templates, variant arguments, and
`initializer_list` for the homogeneous case.

Declaring a `...` parameter is sometimes useful for techniques that do not
involve actual argument passing — notably to declare "take-anything" functions
so as to disable everything else in an overload set, or to express a catch-all
case in a template metaprogram.

## Decision

Do not pass or read arguments through the C variadic mechanism.

- Do not write a function with a C-style `...` parameter that is read with
  `va_start` / `va_arg` / `va_end`.
- Use a variadic template with a fold expression or a recursive expansion where
  the arguments have differing types.
- Use `std::initializer_list<T>`, a `std::span<const T>`, or a container where
  the arguments are homogeneous.
- Use overloading where the set of accepted types is small and fixed, and a
  variant type where the argument must carry its own type at run time.
- Use a type-safe formatting facility rather than a `printf`-style one for
  building strings.
- A `...` parameter used purely for overload resolution, with no argument ever
  read from it, is permitted; that is a declaration technique, not argument
  passing.

## Rationale

The strongest argument is that varargs is the one place where the type system is
switched off by request rather than by accident. Every other unsafe construct in
C++ has a type somewhere that could be checked; `va_arg` has an argument list
with no type information at all, so the caller and the callee agree by
convention or the program has undefined behaviour with no diagnostic anywhere.

The variadic template alternative is strictly better rather than a trade: it is
type-safe, it is more flexible than the `int`-only version it replaces, it
inlines, and it is shorter to write. There is no case in which the old mechanism
is the better tool for passing arguments.

## Consequences

### Positive

- Argument types are checked at the call site, so the `sum(3.14159, 2.71828)`
  class of undefined behaviour cannot be written.
- The replacement is more general: the template version works for every type
  with a suitable operator, where the vararg version worked for one.
- No default argument promotions, so no surprises about `float` becoming
  `double` or a small enum becoming `int` in transit.
- The compiler can inline through a variadic template, which it cannot do
  through a `va_list`.

### Negative

- Variadic templates live in headers and instantiate per argument-type
  combination, so this trades run-time unsafety for compile-time cost and code
  size, and the error messages when a fold expression fails are far worse than
  the vararg version's silence.
- C interoperation is the hole. `printf`, `syslog`, `snprintf`, and most
  operating system logging APIs are vararg functions, and calling them is
  unavoidable at the boundary described in
  [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md); the
  rule can only require that the unsafety be confined there.
- A vararg function cannot be replaced by a template if it must be exported
  across a stable ABI, so the rule has no answer for that case.
- The permitted `...`-for-overload-resolution use means a blanket diagnostic on
  the token is wrong, and any enforcement has to distinguish declaration from
  use.

### Neutral

- `std::format` and similar type-safe facilities supersede the most common
  legitimate reason anyone reached for varargs in the first place.
- The rule concerns C varargs specifically. Template parameter packs are the
  intended replacement and are not restricted by it.

## Enforcement

- Issue a diagnostic for the use of `va_list`, `va_start`, or `va_arg`.
- Issue a diagnostic for passing an argument to a vararg parameter of a function
  that does not offer an overload for a more specific type in the position of
  the vararg. To fix: use a different function, or suppress the diagnostic
  deliberately at that site.

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — varargs
  is the clearest deliberate breach of static type safety in the language.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  template alternative moves the check to compile time, where it costs nothing.
- [0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) —
  the C boundary where vararg calls remain unavoidable.
- [0039](0039-i-30-encapsulate-rule-violations.md) — how a necessary vararg call
  should be wrapped when one is forced on the code.
