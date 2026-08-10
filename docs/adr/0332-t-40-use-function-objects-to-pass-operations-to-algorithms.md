# 0332. T.40: Use function objects to pass operations to algorithms

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Function objects can carry more information through an interface than a plain
pointer to function, and in general passing function objects gives better
performance than passing pointers to functions.

```cpp
bool greater(double x, double y) { return x > y; }
sort(v, greater);                                    // pointer to function: potentially slow
sort(v, [](double x, double y) { return x > y; });   // function object
sort(v, std::greater{});                             // function object

bool greater_than_7(double x) { return x > 7; }
auto x = find_if(v, greater_than_7);                 // pointer to function: inflexible
auto y = find_if(v, [](double x) { return x > 7; }); // function object: carries the needed data
auto z = find_if(v, Greater_than<double>(7));        // function object: carries the needed data
```

Those functions can of course be generalised using `auto` or concepts:

```cpp
auto y1 = find_if(v, [](totally_ordered auto x) { return x > 7; }); // require an ordered type
auto z1 = find_if(v, [](auto x) { return x > 7; });                 // hope that the type has a >
```

Lambdas generate function objects. The performance argument depends on compiler
and optimiser technology.

## Decision

Pass operations to algorithms as objects, not as function pointers.

- Pass a lambda, a standard function object such as `std::greater{}`, or a
  purpose-written class with `operator()`.
- Where the operation depends on a value — a threshold, a tolerance, a
  comparator's locale — capture or store it, instead of writing one free function
  per value.
- Constrain a generic lambda's parameter with a concept where the operation
  assumes anything about the type — `[](totally_ordered auto x)` rather than
  `[](auto x)` — per
  [0321](0321-t-10-specify-concepts-for-all-template-arguments.md).
- Do not take a callable parameter as a function pointer type when a template
  parameter or `std::invocable` constraint will do; a function pointer accepts
  strictly less.

## Rationale

The performance argument is the one usually quoted, but it is conditional on the
optimiser, whereas the expressiveness argument is not: a function pointer has no
state, so an operation parameterised by a value cannot be expressed as one at
all without a global or a `void*` context parameter. Each function object also
has its own type, which is what allows the call to be inlined and the algorithm
to be specialised for it; a function pointer collapses every operation of the
same signature into one type and one indirect call.

## Consequences

### Positive

- The operation can carry the data it needs, so no globals and no extra context
  parameters.
- A distinct type per operation gives the optimiser something to inline.
- The operation is written where it is used, so the reader does not have to look
  up a named predicate elsewhere.
- Standard function objects are transparent and work across argument types
  without a new definition.

### Negative

- Each distinct lambda instantiates the algorithm afresh, which costs compile
  time and code size where a shared function pointer would not.
- The parameter type becomes a template parameter, so the algorithm has to be a
  template and its definition has to be in a header.
- Passing an operation across an ABI or a virtual boundary still needs a
  function pointer or a `std::function`, and the latter reintroduces the
  indirection and possibly an allocation.
- Lambdas capturing by reference are easy to leave dangling when the algorithm
  stores them, the hazard
  [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  describes.

### Neutral

- Whether to name the operation or write it inline is
  [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)'s
  question, not this rule's.
- Upstream's own note qualifies the performance claim as dependent on the
  compiler, consistent with
  [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md).

## Enforcement

- Flag pointer-to-function template arguments.
- Flag pointers to functions passed as arguments to a template, accepting the
  risk of false positives.

## Related

- [0468](0468-f-50-use-a-lambda-when-a-function-wont-do-to-capture-local-variables-or-to-write-a-local-function.md)
  — when a lambda is the right function object.
- [0470](0470-f-52-prefer-capturing-by-reference-in-lambdas-that-will-be-used-locally-including-passed-to-algorithms.md)
  and
  [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  — how the captured data should be held.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md) —
  the performance half of the reason is conditional.
- [0134](0134-c-170-if-you-feel-like-overloading-a-lambda-use-a-generic-lambda.md)
  — generic lambdas, as used in the last examples.
