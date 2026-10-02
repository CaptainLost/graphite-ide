# 0119. C.148: Use dynamic_cast to a pointer type when failure to find the required class is considered a valid alternative

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The `dynamic_cast` conversion allows a test of whether a pointer points at a
polymorphic object that has a given class in its hierarchy. Because failure to
find the class merely returns a null value, it can be tested at run time, which
allows code to choose alternative paths depending on the result. This is the
opposite situation from casting to a reference type, where failure is an error
and the cast should not be used for conditional execution.

The example below describes the `add` function of a `Shape_owner` that takes
ownership of constructed `Shape` objects. The objects are also sorted into
views, according to their geometric attributes. `Shape` does not inherit from
`Geometric_attributes`; only its subclasses do.

```cpp
void add(Shape* const item)
{
  // Ownership is always taken
  owned_shapes.emplace_back(item);
  // Check the Geometric_attributes and add the shape to none/one/some/all of the views
  if (auto even = dynamic_cast<Even_sided*>(item))
  {
    view_of_evens.emplace_back(even);
  }
  if (auto trisym = dynamic_cast<Trilaterally_symmetrical*>(item))
  {
    view_of_trisyms.emplace_back(trisym);
  }
}
```

A shape that is neither is not an error; it simply appears in neither view. A
failure to find the required class causes `dynamic_cast` to return a null value,
and dereferencing a null pointer is undefined behaviour, so the result of a
pointer `dynamic_cast` must always be treated as if it might be null, and
tested.

## Decision

Cast to a pointer type when not finding the class is one of the outcomes the
code is written to handle.

- Use the pointer form whenever the answer feeds a branch; use the reference
  form when failure is a programming error.
- Test the result before every use. Prefer the condition-with-initialiser form,
  `if (auto p = dynamic_cast<D*>(q)) { ... }`, so the pointer's scope is the
  branch in which it is known to be non-null.
- Do not silently ignore the null case; either handle it or record why nothing
  needs doing.
- Do not use a pointer cast and then assume success, which discards the only
  advantage this form has over the reference form.

## Rationale

This rule and its reference-typed counterpart are one decision with two
spellings, and the target type is where the author's intent about failure gets
recorded. Choosing the pointer form commits to writing the test, and the
condition-with-initialiser form makes that commitment structural rather than
remembered — the pointer does not exist outside the branch that proved it valid.
Where several independent interfaces may or may not be present, as in the shapes
example, no single virtual function on the base could have answered the
question, which is what earns this pattern its place.

## Consequences

### Positive

- The absence of the class is an ordinary control-flow case rather than an
  exceptional one, with no unwinding involved.
- One object can be tested against several unrelated interfaces without the base
  class having to know about any of them.
- Scoping the result to the `if` makes the tested and untested states
  syntactically distinct.

### Negative

- Every use costs a null test, and forgetting one is undefined behaviour rather
  than a diagnostic — the reference form has no such hole.
- The pattern encourages a chain of type tests where a virtual function or a
  variant would have been the better design, and each added type extends the
  chain in every place it appears.
- The run-time cost is paid per test, so a series of casts on a hot path is a
  series of hierarchy walks.
- Detecting a missing null test in general requires whole-program analysis, so
  the enforcement below is described upstream as complex, and will not catch
  every case.

### Neutral

- Which of the two forms is correct depends entirely on what the author means by
  failure; the language cannot decide it.
- The rule constrains the shape of the cast, not the decision to navigate the
  hierarchy at all.

## Enforcement

- (Complex) Unless there is a null test on the result of a `dynamic_cast` of a
  pointer type, warn upon dereference of the pointer.

## Related

- [0118](0118-c-147-use-dynamic-cast-to-a-reference-type-when-failure-to-find-the-required-class-is-considered-an-error.md)
  — the form to use when failure is an error.
- [0117](0117-c-146-use-dynamic-cast-where-class-hierarchy-navigation-is-unavoidable.md)
  — the rule that admits the cast in the first place.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — a
  pointer that may be null is precisely what this cast returns, and it must be
  treated as such.
- [0013](0013-p-7-catch-run-time-errors-early.md) — the null test is the check
  that stops a wrong assumption propagating.
