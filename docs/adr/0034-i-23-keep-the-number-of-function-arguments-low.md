# 0034. I.23: Keep the number of function arguments low

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Having many arguments opens opportunities for confusion, and passing lots of
arguments is often costly compared to the alternatives.

The two most common reasons why functions have too many parameters are:

1. **Missing an abstraction.** A compound value is being passed as individual
   elements instead of as a single object that enforces an invariant. This not
   only expands the parameter list, it leads to errors, because the component
   values are no longer protected by an enforced invariant.
2. **Violating "one function, one responsibility."** The function is trying to
   do more than one job and should probably be refactored.

The standard library's `merge()` is at the limit of what can be comfortably
handled:

```cpp
template<class InputIterator1, class InputIterator2, class OutputIterator,
         class Compare>
OutputIterator merge(InputIterator1 first1, InputIterator1 last1,
                     InputIterator2 first2, InputIterator2 last2,
                     OutputIterator result, Compare comp);
```

That is because of the first problem — a missing abstraction. Instead of passing
a range, the STL passed iterator pairs, which are unencapsulated component
values. Defaulting the comparison reduces the surface complexity presented to
most users without reducing the total. To really reduce the number of arguments,
bundle them into higher-level abstractions:

```cpp
template<class InputRange1, class InputRange2, class OutputIterator>
OutputIterator merge(InputRange1 r1, InputRange2 r2, OutputIterator result);
```

Grouping arguments into bundles is a general technique to reduce their number
and to increase the opportunities for checking. Alternatively, a concept can
define the notion of three types that must be usable for merging:

```cpp
template<class In1, class In2, class Out>
  requires mergeable<In1, In2, Out>
Out merge(In1 r1, In2 r2, Out result);
```

The safety profiles recommend replacing

```cpp
void f(int* some_ints, int some_ints_length);  // BAD: C style, unsafe
```

with

```cpp
void f(gsl::span<int> some_ints);              // GOOD: safe, bounds-checked
```

Here, using an abstraction has safety and robustness benefits and naturally also
reduces the number of parameters.

How many parameters are too many? Try to use fewer than four. There are
functions that are best expressed with four individual parameters, but not many.

## Decision

Keep the number of function arguments low — fewer than four, as a working
target.

- When a parameter list grows, ask which of the two causes it is. If a compound
  value is being spelled out, introduce the type that means it. If the function
  is doing two jobs, split it.
- Group related arguments into a meaningful object and pass that, by value or by
  reference.
- Use default arguments or overloads so the most common calls are the shortest,
  while keeping the full form available.
- Treat a range parameter as one argument rather than two, and prefer it to an
  iterator pair.
- A function that needs four or more genuinely independent parameters is
  allowed, and is a place to look twice.

## Rationale

The count is a symptom, and the rule is useful because of what it points at. A
long parameter list is almost always either a type that was never written or a
function that never got split, and both of those are worth fixing for reasons
that have nothing to do with counting.

Bundling also buys checking. The components of a bundled type can maintain an
invariant between them; six loose arguments cannot, and the caller is free to
combine them in ways the function never anticipated.

## Consequences

### Positive

- Call sites are shorter and harder to get wrong.
- Bundled arguments can enforce invariants that loose ones cannot.
- The rule surfaces missing abstractions and overloaded responsibilities, which
  are the real defects.

### Negative

- A bundle type introduced only to shorten a parameter list is a type with no
  behaviour, and can be worse than the arguments it replaced.
- Default arguments hide complexity rather than removing it, and they interact
  badly with overloading.
- Four is arbitrary. Applied literally it encourages contortions in the
  functions that legitimately need five.

### Neutral

- Whether the bundling is done with a struct or with a concept-constrained range
  type is a per-case judgement.
- This record is about interfaces; it says nothing about the length of a
  function body.

## Enforcement

- Warn when a function declares two iterators, including pointers, of the same
  type instead of a range or a view.
- (Not enforceable) The general form is a philosophical guideline that is
  infeasible to check directly.

## Related

- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  settings-struct example is the same fix for a different symptom.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — a `span`
  replaces two parameters with one.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — long lists of same-typed parameters are also swappable.
