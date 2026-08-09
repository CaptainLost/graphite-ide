# 0034. I.23: Keep the number of function arguments low

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

Checked against the tree before adopting: no declaration in any header in Core
or Graphite takes four or more parameters. The rule is already satisfied, and
this record exists to keep it that way.

## Decision

Adopt I.23 as written. Transcribed in full:

> **Reason.** Having many arguments opens opportunities for confusion. Passing
> lots of arguments is often costly compared to alternatives.

**Discussion.** The two most common reasons why functions have too many
parameters are:

1. **Missing an abstraction.** There is an abstraction missing, so that a
   compound value is being passed as individual elements instead of as a single
   object that enforces an invariant. This not only expands the parameter list,
   but it leads to errors because the component values are no longer protected
   by an enforced invariant.
2. **Violating "one function, one responsibility."** The function is trying to
   do more than one job and should probably be refactored.

**Example.** The standard-library `merge()` is at the limit of what we can
comfortably handle:

```cpp
template<class InputIterator1, class InputIterator2, class OutputIterator, class Compare>
OutputIterator merge(InputIterator1 first1, InputIterator1 last1,
                     InputIterator2 first2, InputIterator2 last2,
                     OutputIterator result, Compare comp);
```

Note that this is because of problem 1 above — missing abstraction. Instead of
passing a range (abstraction), STL passed iterator pairs (unencapsulated
component values).

Here, we have four template arguments and six function arguments. To simplify
the most frequent and simplest uses, the comparison argument can be defaulted to
`<`:

```cpp
template<class InputIterator1, class InputIterator2, class OutputIterator>
OutputIterator merge(InputIterator1 first1, InputIterator1 last1,
                     InputIterator2 first2, InputIterator2 last2,
                     OutputIterator result);
```

This doesn't reduce the total complexity, but it reduces the surface complexity
presented to many users. To really reduce the number of arguments, we need to
bundle the arguments into higher-level abstractions:

```cpp
template<class InputRange1, class InputRange2, class OutputIterator>
OutputIterator merge(InputRange1 r1, InputRange2 r2, OutputIterator result);
```

Grouping arguments into "bundles" is a general technique to reduce the number of
arguments and to increase the opportunities for checking.

Alternatively, we could use a standard library concept to define the notion of
three types that must be usable for merging:

```cpp
template<class In1, class In2, class Out>
  requires mergeable<In1, In2, Out>
Out merge(In1 r1, In2 r2, Out result);
```

**Example.** The safety Profiles recommend replacing

```cpp
void f(int* some_ints, int some_ints_length);  // BAD: C style, unsafe
```

with

```cpp
void f(gsl::span<int> some_ints);              // GOOD: safe, bounds-checked
```

Here, using an abstraction has safety and robustness benefits, and naturally
also reduces the number of parameters.

**Note.** How many parameters are too many? Try to use fewer than four (4)
parameters. There are functions that are best expressed with four individual
parameters, but not many.

Alternative: Use better abstraction: Group arguments into meaningful objects and
pass the objects (by value or by reference).

Alternative: Use default arguments or overloads to allow the most common forms
of calls to be done with fewer arguments.

**Enforcement.**

- Warn when a function declares two iterators (including pointers) of the same
  type instead of a range or a view.
- (Not enforceable) This is a philosophical guideline that is infeasible to check
  directly.

## Rationale

Adopted as written, with "fewer than four" taken as the working threshold since
the codebase already meets it.

The Discussion is the part worth keeping, because it turns a count into a
diagnosis: a long parameter list is a symptom of a missing type or of a function
doing two jobs. That reframing is what makes the rule useful in review —
"this has five parameters" is a weak objection; "these three parameters are one
concept without a name" is a strong one.

The second example points at `std::span` and is the same change
[0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) asks for; the two
rules agree, and the span conversion satisfies both.

## Consequences

### Positive

- Currently satisfied everywhere, so the rule costs nothing today and gives a
  clear ceiling for new signatures.
- The bundling advice has a ready subject: `DocumentSet::Add(path, bool, bool)`
  is three parameters, under the threshold, and still the signature most
  improved by grouping — see
  [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md).

### Negative

- A hard count can be gamed: bundling two parameters into a struct to get under
  four moves the confusion into the struct without removing it. The rule's
  Discussion guards against this, but the number is what people remember.

### Neutral

- The one mechanical enforcement point, about two iterators of the same type,
  has no subject here; the codebase does not pass iterator pairs across
  interfaces.

## Related

- [0033](0033-i-22-avoid-complex-initialization-of-global-objects.md) — the
  previous rule.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — the next rule.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the span
  change that serves both rules.
