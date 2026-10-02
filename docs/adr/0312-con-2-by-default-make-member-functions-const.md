# 0312. Con.2: By default, make member functions const

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section Con (Constants and immutability), transcribed 2026-08-10

## Context

A member function should be marked `const` unless it changes the object's
observable state. This gives a more precise statement of design intent, better
readability, more errors caught by the compiler, and sometimes more optimisation
opportunities.

```cpp
class Point {
    int x, y;
public:
    int getx() { return x; }    // BAD, should be const as it doesn't modify the object's state
    // ...
};
void f(const Point& pt)
{
    int x = pt.getx();          // ERROR, doesn't compile because getx was not marked const
}
```

The omission is not a cosmetic one: it makes the class unusable through a
reference to `const`, which is how callers following
[0313](0313-con-3-by-default-pass-pointers-and-references-to-consts.md) will hold
it.

It is not inherently bad to pass a pointer or reference to non-`const`, but that
should be done only when the called function is supposed to modify the object. A
reader must assume that a function taking a plain `T*` or `T&` will modify what
it refers to; and even if it does not now, it might later, without forcing a
recompilation of its callers.

Some libraries declare a `T*` parameter even though the function does not modify
the `T`, which is a problem when modernising code around them. There are three
responses: update the library to be `const`-correct, which is the preferred
long-term solution; cast away `const`, which is best avoided; or provide a
wrapper.

```cpp
void f(int* p);   // old code: f() does not modify `*p`
void f(const int* p) { f(const_cast<int*>(p)); } // wrapper
```

The wrapper is a patch, to be used only when the declaration of `f()` cannot be
modified — because it lives in a library that is not yours to change.

Finally, `const` is a claim about observable state, not about bytes. A `const`
member function may modify a member declared `mutable`, or an object reached
through a pointer member. The common use is memoisation:

```cpp
class Date {
public:
    // ...
    const string& string_ref() const
    {
        if (string_val == "") compute_string_rep();
        return string_val;
    }
    // ...
private:
    void compute_string_rep() const;    // compute string representation and place it in string_val
    mutable string string_val;
    // ...
};
```

Another way of saying this is that constness is not transitive: a `const` member
function can change `mutable` members and objects reached through non-`const`
pointers. It is the job of the class to ensure such mutation happens only where
it is consistent with the invariants it offers its users.

## Decision

Mark a member function `const` unless it changes the object's observable state.

- Apply this to every observer: accessors, comparisons, size and emptiness
  queries, printing, serialisation, validity checks.
- Treat a missing `const` as a defect in the class rather than an inconvenience
  at the call site, and fix it at the declaration.
- Where a function must cache or memoise, declare the cached member `mutable`,
  keep the mutation entirely inside the class, and keep the observable result
  identical to what the uncached computation would return.
- Where a third-party declaration is not `const`-correct and cannot be changed,
  wrap it in a `const`-taking overload that performs the `const_cast` in one
  place, and do not spread the cast across call sites.
- Do not add `const` to a member function that mutates state a caller can
  observe, in order to make a call compile.

## Rationale

`const` on a member function is not an annotation on the function, it is part of
the type's interface: it decides whether the class can be used at all through a
reference to `const`. `Point::getx` is the whole argument in miniature — one
missing keyword propagates outward and forces every caller to hold a mutable
reference, which then forces their callers to, until the promise of immutability
has been lost from a whole call graph on the strength of an accessor.

The `mutable` allowance is what makes the rule workable rather than a slogan.
Constness is defined against the invariants the class publishes, not against its
representation, which is exactly why the class — and not its callers — is the
right place to decide what counts as a change.

## Consequences

### Positive

- The class becomes usable through references to `const`, which is what most of
  the other rules in this section assume.
- Attempts to modify the object from an observer become compile errors.
- The split between observers and modifiers is documented in the signatures
  rather than in comments, and the compiler keeps it honest.
- Some optimisations become available that the compiler could not otherwise
  justify.

### Negative

- `const` is viral in the awkward direction: a `const` member function can call
  only other `const` member functions, so correcting one accessor can produce a
  diff far larger than the change that prompted it.
- `const` now implies thread-safe usage in practice, because callers assume a
  `const` object may be shared. The memoising `Date` above is a data race if two
  threads call `string_ref()` on the same object, and the keyword conceals that
  rather than revealing it.
- `mutable` is an unchecked escape. Nothing verifies that a `mutable` member is
  invisible to users; that judgement is the class author's, and a wrong one
  makes the `const` a lie.
- The `const_cast` wrapper is only as sound as the comment above it. If the
  wrapped function does modify the object, the call is undefined behaviour where
  the underlying object was itself declared `const`, and breaks the promise made
  to callers everywhere else; and the wrapper is precisely the case where the
  function's implementation is not yours to inspect.

### Neutral

- The rule concerns observable state, so a class may legitimately do work inside
  a `const` member function, provided the result of every observer is unchanged.
- Whether an accessor should exist at all is a separate question, covered by
  [0106](0106-c-131-avoid-trivial-getters-and-setters.md).
- The pointer-to-implementation idiom, where a `const` member function holds a
  non-`const` pointer to the implementation object, is the standard illustration
  that constness is not transitive; see
  [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md).

## Enforcement

Flag a member function that is not marked `const` but performs no non-`const`
operation on any member variable.

## Related

- [0311](0311-con-1-by-default-make-objects-immutable.md) — the same default
  applied to objects.
- [0313](0313-con-3-by-default-pass-pointers-and-references-to-consts.md) — the
  rule that makes a non-`const` member function unusable at the call site.
- [0106](0106-c-131-avoid-trivial-getters-and-setters.md) — before marking an
  accessor `const`, ask whether it should exist.
- [0038](0038-i-27-for-stable-library-abi-consider-the-pimpl-idiom.md) — the
  idiom where non-transitive constness is felt most sharply.
