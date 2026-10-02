# 0462. F.43: Never (directly or indirectly) return a pointer or a reference to a local object

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

After the return from a function, its local objects no longer exist. A pointer
to one is a dangling pointer, and using it invites crashes and data corruption.

```cpp
int* f()
{
    int fx = 9;
    return &fx;  // BAD
}

void g(int* p)   // looks innocent enough
{
    int gx;
    cout << "*p == " << *p << '\n';
    *p = 999;
    cout << "gx == " << gx << '\n';
}

void h()
{
    int* p = f();
    int z = *p;  // read from abandoned stack frame (bad)
    g(p);        // pass pointer to abandoned stack frame to function (bad)
}
```

On one popular implementation this printed:

```
*p == 999
gx == 999
```

which is what one would expect: the call of `g()` reuses the stack space
abandoned by the call of `f()`, so `*p` refers to the space now occupied by
`gx`. Imagine what would happen if `fx` and `gx` were of different types, or if
either was a type with an invariant, or if the dangling pointer were passed
around among a larger set of functions, or what a cracker could do with it.
Fortunately most modern compilers catch and warn against this simple case.

The same applies to references:

```cpp
int& f()
{
    int x = 7;
    // ...
    return x;  // Bad: returns reference to object that is about to be destroyed
}
```

It applies only to non-static local variables. All static variables are, as
their name indicates, statically allocated, so pointers to them cannot dangle.

Not all examples of leaking a pointer to a local are that obvious:

```cpp
int* glob;       // global variables are bad in so many ways

template<class T>
void steal(T x)
{
    glob = x();  // BAD
}

void f()
{
    int i = 99;
    steal([&] { return &i; });
}

int main()
{
    f();
    cout << *glob << '\n';
}
```

Here the location abandoned by the call of `f` is read. The pointer stored in
`glob` could be used much later and cause trouble in unpredictable ways.

The address of a local variable can be returned or leaked by a return statement,
by a `T&` out-parameter, as a member of a returned object, as an element of a
returned array, and in other ways besides. Similar examples can be constructed
leaking a pointer from an inner scope to an outer one, and are handled
equivalently. A slightly different variant of the problem is placing pointers in
a container that outlives the objects pointed to. Pointer invalidation is
another route to a dangling pointer, and is detected and prevented by similar
techniques.

## Decision

Never let the address of a non-static local object escape the scope that owns
it, by any route.

- Do not return `&local` or `T&` bound to a local. Return by value; copy elision
  makes this free in the ordinary case.
- Do not write a local's address through a `T*` or `T&` out-parameter, into a
  member of a returned object, or into an element of a returned container.
- Do not store a local's address in a global, a static, a member of a
  longer-lived object, or a container that outlives the scope.
- Do not let a lambda that captures a local by reference outlive that local; see
  [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md).
  The `steal` example escapes through a lambda's return value, not through
  `f`'s.
- Apply the same reasoning to an inner block scope, not only to a function body.
- Where the caller needs the object to persist, give it ownership of one:
  return by value, or return an owning pointer per
  [0459](0459-f-26-use-a-unique-ptr-t-to-transfer-ownership-where-a-pointer-is-needed.md).

## Rationale

What makes this rule worth a record, rather than an obvious remark, is the
`steal` example. The direct form is caught by every compiler; the indirect form
is not caught by any of them, because no single function in it is wrong. `f`
takes an address of a local, which is legal. `steal` assigns a `T`, which is
legal. Composition produces undefined behaviour that neither function can be
blamed for.

The printed output is the second half of the argument. The program did not
crash. It produced plausible numbers, and the corruption showed up in an
unrelated variable in an unrelated frame — which is the reason this class of bug
is expensive rather than merely wrong.

## Consequences

### Positive

- The most common source of use-after-free in otherwise modern C++ is removed at
  the point where it is cheapest to see.
- Returning by value instead is usually not slower, because of guaranteed copy
  elision and move semantics.
- It forces the ownership question to be answered at the boundary rather than
  deferred to the caller.

### Negative

- The rule is easy to state and hard to check. The indirect forms — through
  members, containers, out-parameters, and lambdas — defeat compiler diagnostics
  and need whole-program lifetime analysis that few builds actually run.
- Returning by value where a reference was returned before can cost a copy in
  the cases elision does not cover, particularly for a member returned from a
  branch.
- The static-variable exception is real but narrow, and leaning on it conflicts
  with [0021](0021-i-2-avoid-non-const-global-variables.md).
- Nothing here helps with the related problem the source names but does not
  solve: a pointer that was valid when returned and is invalidated later by a
  reallocation.

### Neutral

- Returning a reference is not itself forbidden; returning one to a *local* is.
  [0463](0463-f-44-return-a-t-when-copy-is-undesirable-and-returning-no-object-isnt-needed.md)
  covers the legitimate case, where the referent outlives the call.
- The rule is about lifetime, not about pointers. Iterators, `span`, and
  `string_view` dangle in exactly the same way and are covered by the same
  reasoning.

## Enforcement

- Compilers tend to catch return of a reference to a local, and could in many
  cases catch return of a pointer to one.
- Static analysis can catch many common patterns in the use of pointers that
  indicate positions, thereby eliminating dangling pointers.

## Related

- [0461](0461-f-42-return-a-t-to-indicate-a-position-only.md) — a returned
  position must be a position in something that outlives the call.
- [0471](0471-f-53-avoid-capturing-by-reference-in-lambdas-that-will-be-used-non-locally-including-returned-stored-on-the-heap-or-passed-to-another-thread.md)
  — the lambda form of the same escape, as in the `steal` example.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the resource-lifetime
  principle this rule enforces at a function boundary.
- [0013](0013-p-7-catch-run-time-errors-early.md) — why the deferred, plausible
  wrong answer is worse than an immediate failure.
