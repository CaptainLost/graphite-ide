# 0012. P.6: What cannot be checked at compile time should be checkable at run time

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Leaving hard-to-detect errors in a program is asking for crashes and bad
results. Ideally every error that is not an error in the programmer's logic is
caught at either compile time or run time. It is impossible to catch all errors
at compile time, and often not affordable to catch all remaining errors at run
time. What is achievable is a design in which the errors *could* be caught,
given sufficient resources — analysis tools, run-time checks, machine time.

That is a property of interfaces, and the upstream examples are all interfaces
that destroy it. Passing a pointer with no extent:

```cpp
// separately compiled, possibly dynamically loaded
extern void f(int* p);
void g(int n)
{
    // bad: the number of elements is not passed to f()
    f(new int[n]);
}
```

A crucial bit of information, the number of elements, has been so thoroughly
obscured that static analysis is probably infeasible, and dynamic checking can
be very difficult when `f()` is part of an ABI and the pointer cannot be
instrumented. Helpful information could be embedded into the free store, but
that requires global changes to a system and maybe to the compiler. This is a
design that makes error detection very hard.

Passing the count alongside is better, and still not checkable:

```cpp
extern void f2(int* p, int n);
void g2(int n)
{
    f2(new int[n], m);  // bad: a wrong number of elements can be passed to f()
}
```

Passing the number of elements as an argument is better, and far more common,
than relying on some unstated convention for discovering it. But a simple typo
introduces a serious error: the connection between the two arguments is
conventional rather than explicit. It is also left implicit whether `f2()` is
supposed to delete its argument.

The standard resource-management pointers do not help when they point at an
array:

```cpp
extern void f3(unique_ptr<int[]>, int n);
void g3(int n)
{
    f3(make_unique<int[]>(n), m);    // bad: pass ownership and size separately
}
```

What works is passing the pointer and the number of elements as one object:

```cpp
extern void f4(vector<int>&);
extern void f4(span<int>);
void g3(int n)
{
    vector<int> v(n);
    f4(v);                     // pass a reference, retain ownership
    f4(span<int>{v});          // pass a view, retain ownership
}
```

This design carries the number of elements along as an integral part of an
object, so that errors are unlikely and dynamic checking is always feasible, if
not always affordable. The same applies when ownership is transferred:

```cpp
vector<int> f5(int n)          // OK: move
{
    vector<int> v(n);
    // ... initialize v ...
    return v;
}
unique_ptr<int[]> f6(int n)    // bad: loses n
{
    auto p = make_unique<int[]>(n);
    // ... initialize *p ...
    return p;
}
owner<int*> f7(int n)          // bad: loses n and we might forget to delete
{
    owner<int*> p = new int[n];
    // ... initialize *p ...
    return p;
}
```

## Decision

Design interfaces so that what cannot be checked at compile time remains
checkable at run time.

- Pass a range as one object that carries its extent — `span`, a container
  reference, or a container by value — never as a pointer with the extent
  implied or passed separately.
- When transferring ownership of a sequence, transfer an object that knows its
  own size. Returning `vector<int>` keeps the size; returning
  `unique_ptr<int[]>` or an owning raw pointer discards it.
- Do not make the validity of one argument depend on an unstated convention
  relating it to another.

Whether the checks are actually compiled in is a separate, configurable
question. This record requires that the information needed to check is present,
not that every build pays for the check.

## Rationale

This is the rule that makes checking *possible*; without it, later decisions
about assertions, contracts and hardened builds have nothing to work with. An
interface that has thrown the extent away cannot be rescued by any amount of
diligence at the call site, and if it crosses an ABI it cannot be rescued by
tooling either.

Keeping ownership and extent in the same object also removes the second implicit
convention in the bad examples — who deletes — which is why the fix reads as a
simplification rather than as added ceremony.

## Consequences

### Positive

- Bounds and ownership are recoverable at run time, so checks can be added later
  without changing every interface.
- A whole class of typo — the mismatched count — becomes unexpressible.
- Static analysis has something to reason about at the boundary.

### Negative

- Range and container types are heavier than a bare pointer at an ABI boundary,
  and some boundaries — C APIs, dynamically loaded modules — cannot take them at
  all.
- `span` assumes ABI compatibility between caller and callee: the same compiler
  family and the same standard-library implementation. Across a real plugin
  boundary that assumption fails, and the `(pointer, count)` form comes back for
  reasons this rule cannot argue away.
- Checkable is not checked. The design cost is paid whether or not any build
  ever turns the checks on.

### Neutral

- *When* to check is [0013](0013-p-7-catch-run-time-errors-early.md); this
  record only requires that checking be feasible.
- Existing `(pointer, count)` interfaces retained for compatibility are a known
  and accepted exception, flagged rather than fixed.

## Enforcement

- Flag `(pointer, count)`-style interfaces. This will flag a lot of examples
  that cannot be fixed for compatibility reasons.

## Related

- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  checks that never need to reach run time.
- [0013](0013-p-7-catch-run-time-errors-early.md) — where the feasible check
  should actually be placed.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the ownership half of the same
  examples.
