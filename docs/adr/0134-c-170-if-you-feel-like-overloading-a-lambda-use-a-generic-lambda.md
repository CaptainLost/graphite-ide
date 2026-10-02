# 0134. C.170: If you feel like overloading a lambda, use a generic lambda

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

You cannot overload by defining two different lambdas with the same name. The
overloading section of the guidelines opens by drawing exactly this line:
ordinary functions, function templates and operators can be overloaded; function
objects cannot. A lambda is a function object bound to a variable, and a
variable name in a scope refers to one thing.

```cpp
void f(int);
void f(double);

auto f = [](char);   // error: cannot overload variable and function

auto g = [](int) { /* ... */ };
auto g = [](double) { /* ... */ };   // error: cannot overload variables

auto h = [](auto) { /* ... */ };   // OK
```

The generic lambda is not a workaround for the restriction so much as the
correct expression of the intent. A lambda with an `auto` parameter has a
templated call operator, so one object handles every argument type for which its
body is valid — which is what wanting two same-named lambdas was reaching for.

## Decision

Where an overloaded lambda seems wanted, write one generic lambda.

- Give the lambda an `auto` parameter, or a C++20 template parameter list, and
  let the single body cover the argument types.
- Do not attempt to declare two lambdas with the same name, and do not shadow a
  function with a lambda of the same name.
- Where the bodies genuinely differ per type, branch inside the generic lambda
  with `if constexpr`, or use an overload set built by inheriting from several
  lambdas, rather than trying to redeclare the name.
- Where the operation is a real overload set that callers should be able to
  extend, write named functions instead; a lambda is the wrong construct for
  that.

## Rationale

The restriction is not arbitrary and is not worth fighting: a lambda introduces
a variable, and C++ has never allowed two variables of one name in one scope.
Recognising that the wish for an overloaded lambda is really a wish for one
callable that accepts several types turns a language limitation into a simpler
design, and it is the only such rule in this group where the compiler settles
the argument outright.

## Consequences

### Positive

- One object, one name, no ambiguity about which is in scope where.
- The generic form covers argument types that were not anticipated when the
  lambda was written.
- The mistake is a compile error, so no discipline is needed to avoid it.

### Negative

- A generic lambda accepts anything its body compiles for, so it silently
  accepts types that were never meant to be passed and reports the mismatch deep
  inside the body rather than at the call.
- The bodies that differ per type end up inside one function with `if
  constexpr`, which is less readable than two separate overloads would have
  been.
- Error messages from a generic lambda are template error messages, which are
  substantially worse than those from an ordinary overload set.
- Constraining a generic lambda properly means writing a `requires` clause on an
  unnamed type, which is more ceremony than the two overloads that were wanted.

### Neutral

- C++20 adds explicit template parameter lists for lambdas and allows concepts
  on `auto` parameters, which give a generic lambda a way to constrain what it
  accepts without being overloaded.
- The rule concerns lambdas only; ordinary functions and function templates
  overload normally.

## Enforcement

- The compiler catches the attempt to overload a lambda.

## Related

- [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md) — the
  generic lambda is one name over several types, which is what that rule asks
  for.
- [0128](0128-c-163-overload-only-for-operations-that-are-roughly-equivalent.md)
  — an unconstrained generic lambda accepts unrelated types, which is what that
  rule warns against.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — a generic lambda is a template, and its parameter deserves a concept.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — this
  rule is enforced entirely at compile time.
