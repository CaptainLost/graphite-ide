# 0354. T.100: Use variadic templates when you need a function that takes a variable number of arguments of a variety of types

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Variadic templates are the most general mechanism for a function that takes a
variable number of arguments of differing types, and they are both efficient
and type-safe. The alternative — C's `varargs`, `<cstdarg>` and `va_arg` — is
neither: the types are not recorded, the callee recovers them from a format
string or a convention, and getting it wrong is undefined behaviour rather than
a diagnostic.

Upstream illustrates the rule with a bare reference to `printf` and leaves the
example unwritten, so the argument above is the whole of what it gives. Its one
concrete instruction is the enforcement: flag `va_arg`.

## Decision

Write a function template with a parameter pack when the argument count and
argument types both vary.

- Declare the pack as `template<class... Ts> void f(Ts&&... args)` and forward
  it with `std::forward<Ts>(args)...` rather than copying.
- Do not define a C-style variadic function, and do not use `va_arg` to read
  one.
- Constrain the pack where the arguments must satisfy something — with a
  concept, a fold over a trait, or a `requires` clause — so a bad call is
  rejected at the interface rather than deep inside the body.
- Where the arguments are all of one type, use a sequence type instead; the
  pack is for genuine heterogeneity.

## Rationale

A parameter pack keeps the static type of every argument, which is exactly the
information a C-style variadic function throws away and then asks the caller to
restate in a format string. Because the types survive, the compiler checks the
call, the conversions are the ordinary ones, and any type at all can be passed:
sending a class with a non-trivial copy constructor or destructor through an
ellipsis is conditionally supported at best, with semantics the implementation
chooses.

## Consequences

### Positive

- Every argument's type is known to the callee, so calls are type-checked.
- Non-trivially-copyable types can be passed, which C varargs cannot do.
- Perfect forwarding is available, so nothing is copied that need not be.

### Negative

- The function must live in a header, and it instantiates once per distinct
  argument list, so a widely used variadic interface costs compile time and
  binary size.
- Error messages from a pack that fails to satisfy an unstated requirement are
  produced at the point of instantiation and are famously long; constraining
  the pack helps but is extra work.
- Processing a pack is awkward — fold expressions, recursion or an
  initializer-list trick — and the resulting code is harder to read than the
  loop a C variadic function would have used.
- An unconstrained variadic template is an aggressive overload candidate and
  can capture calls intended for other functions.

### Neutral

- Interoperating with a C API that is itself variadic still requires the old
  mechanism at the boundary.
- The rule says nothing about how the arguments are passed or processed; those
  are separate rules, which upstream leaves largely unwritten.

## Enforcement

- Flag uses of `va_arg` in user code.

## Related

- [0196](0196-es-34-dont-define-a-c-style-variadic-function.md) — the
  prohibition this rule is the positive form of.
- [0473](0473-f-55-dont-use-va-arg-arguments.md) — the same prohibition from the
  caller's side.
- [0357](0357-t-103-dont-use-variadic-templates-for-homogeneous-argument-lists.md)
  — the limit on this rule: variety of types is the precondition.
