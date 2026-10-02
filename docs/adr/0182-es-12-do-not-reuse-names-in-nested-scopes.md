# 0182. ES.12: Do not reuse names in nested scopes

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

When the same name exists in an outer and an inner scope, it is easy to get
confused about which variable is being used, and that confusion becomes a
maintenance problem.

```cpp
int d = 0;
// ...
if (cond) {
    // ...
    d = 9;
    // ...
}
else {
    // ...
    int d = 7;
    // ...
    d = value_to_be_returned;
    // ...
}
return d;
```

If this is a large `if`-statement, it is easy to overlook that a new `d` has
been introduced in the inner scope, and the assignment to it is then read as
setting the value that will be returned. It does not. This is a known source of
bugs; such reuse of a name in an inner scope is called shadowing.

Shadowing is primarily a problem when functions are too large and too complex —
in a six-line function the second declaration is visible.

The language already disallows the most confusing case, shadowing of function
arguments in the outermost block:

```cpp
void f(int x)
{
    int x = 4;  // error: reuse of function argument name
    if (x) {
        int x = 7;  // allowed, but bad
        // ...
    }
}
```

Reuse of a member name as a local variable is the same problem across a
different pair of scopes:

```cpp
struct S {
    int m;
    void f(int x);
};

void S::f(int x)
{
    m = 7;    // assign to member
    if (x) {
        int m = 9;
        // ...
        m = 99; // assign to local variable
        // ...
    }
}
```

The exception is deliberate and structural: function names from a base class are
often reused in a derived class.

```cpp
struct B {
    void f(int);
};

struct D : B {
    void f(double);
    using B::f;
};
```

This is error-prone in its own right. Had the `using` declaration been
forgotten, a call `d.f(1)` would not have found the `int` version of `f` — the
derived declaration hides the whole overload set from the base, and the `int`
argument would convert to `double` silently.

## Decision

Do not introduce a name that hides another name visible at that point.

- Do not declare a local variable with the same name as a variable in an
  enclosing block, as a function parameter, or as a member of the enclosing
  class.
- Do not declare a local variable with the same name as a file-scope or
  namespace-scope entity that the function uses.
- When a derived class declares a function whose name exists in a base, add a
  `using Base::f;` declaration so the base overloads remain visible, or give the
  derived function a different name.
- Where the compiler offers a shadowing warning, turn it on and treat it as an
  error.
- If a name genuinely wants to be reused, that is a signal the function is too
  long; split it rather than renaming to `d2`.

## Rationale

Shadowing is the one naming mistake that changes program behaviour silently and
compiles cleanly by design. The example above is not a program that fails to
build or crashes — it returns the wrong value, and the reviewer's eye goes to
`d = value_to_be_returned;` and confirms exactly what they expected to see. No
amount of care at the assignment can catch it; only not creating the second `d`
can.

## Consequences

### Positive

- An identifier means one thing throughout a function, so reading an assignment
  does not require scanning upwards for a shadowing declaration.
- Refactoring becomes safer: moving a statement between blocks cannot change
  which variable it touches.
- The `using Base::f;` requirement makes hidden base overloads visible, which
  fixes a second class of silent-conversion bugs.

### Negative

- Conventional short names — `i`, `n`, `s` — are exactly the ones this rule
  forbids reusing, so it pulls against
  [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  and can force names like `inner_i`.
- Member names are in scope throughout every member function, so a strict
  reading forbids common local names in any class with common member names; a
  member prefix convention is the usual answer, and this record does not impose
  one.
- Compiler shadowing warnings fire on third-party headers as readily as on
  local code, so enabling them can require suppression at include boundaries.
- The rule does not remove the underlying cause, which is function length; it
  makes long functions marginally safer rather than shorter.

### Neutral

- Reuse of base-class function names in a derived class is explicitly excepted,
  with the `using` declaration as the price.
- Upstream raises but does not settle whether a separate rule is needed for
  hiding within class hierarchies; nothing further is transcribed here.

## Enforcement

- Flag reuse of a name in nested local scopes.
- Flag reuse of a member name as a local variable in a member function.
- Flag reuse of a global name as a local variable or a member name.
- Flag reuse of a base class member name in a derived class, except for function
  names.

## Related

- [0175](0175-es-5-keep-scopes-small.md) — a small scope is one in which
  shadowing is visible.
- [0177](0177-es-7-keep-common-and-local-names-short-and-keep-uncommon-and-non-local-names-longer.md)
  — the naming rule this one is in tension with.
- [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) — the
  related confusion of one name meaning two things over time.
- [0178](0178-es-8-avoid-similar-looking-names.md) — the same misreading
  produced by spelling rather than by nesting.
