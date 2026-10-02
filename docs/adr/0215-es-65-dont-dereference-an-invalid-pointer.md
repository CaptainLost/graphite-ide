# 0215. ES.65: Don't dereference an invalid pointer

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Dereferencing an invalid pointer, such as `nullptr`, is undefined behaviour,
typically leading to immediate crashes, wrong results, or memory corruption.

This is an obvious and well-known language rule, but it can be hard to follow.
It takes good coding style, library support and static analysis to eliminate
violations without major overhead. Use RAII to avoid lifetime problems, use
`unique_ptr` and `shared_ptr` to avoid lifetime problems, use references where
`nullptr` is not a possibility, use a not-null wrapper to catch an unexpected
`nullptr` early, and use the bounds profile to avoid range errors.

The simplest form of the problem is a pointer that outlives what it points to:

```cpp
void f()
{
    int x = 0;
    int* p = &x;
    if (condition()) {
        int y = 0;
        p = &y;
    } // invalidates p
    *p = 42;            // BAD, p might be invalid if the branch was taken
}
```

To resolve it, either extend the lifetime of the object the pointer is intended
to refer to, or shorten the lifetime of the pointer by moving the dereference
before the pointed-to object's lifetime ends:

```cpp
void f1()
{
    int x = 0;
    int* p = &x;
    int y = 0;
    if (condition()) {
        p = &y;
    }
    *p = 42;            // OK, p points to x or y and both are still in scope
}
```

Unfortunately most invalid-pointer problems are harder to spot and harder to
fix:

```cpp
void f(int* p)
{
    int x = *p; // BAD: how do we know that p is valid?
}
```

There is a huge amount of such code. Most of it works, after a great deal of
testing, but in isolation it is impossible to tell whether `p` could be null.
One approach is to handle the case:

```cpp
void f1(int* p) // deal with nullptr
{
    if (!p) {
        // deal with nullptr (allocate, return, throw, make p point to something, whatever
    }
    int x = *p;
}
```

There are three problems with testing for null: it is not always obvious what to
do if null is found; the test can be redundant and relatively expensive; and it
is not obvious whether the test protects against a contract violation or is part
of the required logic. Stating the precondition instead distinguishes those
cases:

```cpp
void f2(int* p) // state that p is not supposed to be nullptr
{
    assert(p);
    int x = *p;
}
```

That carries a cost only when assertion checking is enabled, and gives the
compiler and the analyser useful information. Alternatively, a not-null wrapper
puts the requirement in the type:

```cpp
void f(not_null<int*> p)
{
    int x = *p;
}
```

These remedies take care of `nullptr` only, and there are other ways to get an
invalid pointer — a pointer to something already deleted:

```cpp
void f(int* p)  // old code, doesn't use owner
{
    delete p;
}

void g()        // old code: uses naked new
{
    auto q = new int{7};
    f(q);
    int x = *q; // BAD: dereferences invalid pointer
}
```

and a pointer into a container that has since reallocated:

```cpp
void f()
{
    vector<int> v(10);
    int* p = &v[5];
    v.push_back(99); // could reallocate v's elements
    int x = *p; // BAD: dereferences potentially invalid pointer
}
```

## Decision

Do not dereference a pointer whose validity is not established by construction,
by contract, or by the type.

- Prefer a reference or a value where null is not a legal state, so the question
  never arises.
- Where a pointer parameter must not be null, say so with a not-null wrapper
  type — see
  [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — or,
  failing that, assert the precondition at entry.
- Distinguish a precondition check from required logic. A null that indicates a
  caller bug is asserted; a null that the environment can legitimately produce
  is handled and reported as an ordinary outcome.
- Do not take the address of a local and let it escape the block that owns it.
  Hoist the object's declaration instead, so its lifetime covers every use of
  the pointer.
- Do not keep a pointer or reference into a container across any operation that
  can reallocate or erase.
- Own heap objects with `unique_ptr` or `shared_ptr` so that a dangling pointer
  to a deleted object is not producible in ordinary code; see
  [0210](0210-es-60-avoid-new-and-delete-outside-resource-management-functions.md).

## Rationale

The four examples describe four different problems that happen to share one
symptom, and only the first of them is about null. Extending a local's lifetime,
declaring a precondition, owning a heap object, and not caching an interior
pointer across a mutation are unrelated disciplines; the rule is really an index
into the lifetime rules rather than a rule about the `*` operator.

The most useful distinction the source draws is between a test and a
precondition. `if (!p)` and `assert(p)` look similar and mean opposite things:
one says null is expected and must be handled, the other says null is a bug in
the caller. Code that uses the first for both loses the ability to say which
callers are wrong.

## Consequences

### Positive

- Pushes the question from the dereference site, where it cannot be answered, to
  the interface, where it can.
- Distinguishing preconditions from handled cases makes the null tests that
  remain meaningful, and lets the redundant ones be removed.
- Ownership types eliminate the delete-then-dereference case entirely rather
  than asking anyone to remember it.

### Negative

- The rule is not fully checkable. Lifetime analysis catches the local cases and
  misses anything that crosses a translation unit, so a large part of the
  guarantee rests on convention.
- `assert` compiles away in release builds, so the mechanism that documents the
  precondition is also the mechanism that stops checking it exactly where
  failures are costliest.
- A not-null wrapper covers null and nothing else. A dangling pointer wrapped in
  it is still dangling, and now looks validated.
- Iterator and pointer invalidation by container operations is invisible in the
  type system, so the last example remains a matter of the author remembering a
  table of invalidation rules.
- The upstream text offers a contract syntax, `[[expects: p]]`, which C++20 does
  not provide; until the language has contracts the available mechanism is an
  assertion, with the release-build limitation noted above.

### Neutral

- Handling null by returning an error, allocating, or substituting a default are
  all acceptable; the rule requires that the choice be made, not which one.
- Spelling the null constant correctly is a separate and much smaller matter,
  covered by [0204](0204-es-47-use-nullptr-rather-than-0-or-null.md).

## Enforcement

This rule is part of the lifetime safety profile.

- Flag a dereference of a pointer that points to an object that has gone out of
  scope.
- Flag a dereference of a pointer that might have been invalidated by assigning
  `nullptr`.
- Flag a dereference of a pointer that might have been invalidated by a
  `delete`.
- Flag a dereference of a pointer to a container element that might have been
  invalidated by a container operation.

## Related

- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  type-level statement of the null half of this rule.
- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the ownership discipline that removes the dangling case.
- [0149](0149-r-3-a-raw-pointer-a-t-is-non-owning.md) — what a raw pointer
  means, and therefore what it does not promise about lifetime.
- [0204](0204-es-47-use-nullptr-rather-than-0-or-null.md) — the spelling rule
  that this one is often confused with.
