# 0012. P.6: What cannot be checked at compile time should be checkable at run time

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

The rule argues against `(pointer, count)` interfaces, which is exactly the shape
Win32 forces on the code in `DirectoryWatcher` and `FileDialog`, and the shape
several internal buffer-handling functions inherited from it.

## Decision

Adopt P.6 as written. Transcribed in full:

> **Reason.** Leaving hard-to-detect errors in a program is asking for crashes
> and bad results.

**Note.** Ideally, we catch all errors (that are not errors in the programmer's
logic) at either compile time or run time. It is impossible to catch all errors
at compile time and often not affordable to catch all remaining errors at run
time. However, we should endeavor to write programs that in principle can be
checked, given sufficient resources (analysis programs, run-time checks, machine
resources, time).

**Example, bad.**

```cpp
// separately compiled, possibly dynamically loaded
extern void f(int* p);

void g(int n)
{
    // bad: the number of elements is not passed to f()
    f(new int[n]);
}
```

Here, a crucial bit of information (the number of elements) has been so
thoroughly "obscured" that static analysis is probably rendered infeasible and
dynamic checking can be very difficult when `f()` is part of an ABI so that we
cannot "instrument" that pointer. We could embed helpful information into the
free store, but that requires global changes to a system and maybe to the
compiler. What we have here is a design that makes error detection very hard.

**Example, bad.** We can of course pass the number of elements along with the
pointer:

```cpp
// separately compiled, possibly dynamically loaded
extern void f2(int* p, int n);

void g2(int n)
{
    // bad: the wrong number of elements can be passed to f2()
    f2(new int[n], n);
}
```

Passing the number of elements as an argument is better (and far more common)
than just passing the pointer and relying on some (unstated) convention for
knowing or discovering the number of elements. However (as shown), a simple typo
can introduce a serious error. The connection between the two arguments of
`f2()` is conventional, rather than explicit.

Also, it is implicit that `f2()` is supposed to `delete` its argument (or did
the caller make a second mistake?).

**Example, bad.** The standard library resource management pointers fail to pass
the size when they point to an object:

```cpp
// separately compiled, possibly dynamically loaded
// NB: this assumes the calling code is ABI-compatible, using a
// compatible C++ compiler and the same stdlib implementation
extern void f3(unique_ptr<int[]>, int n);

void g3(int n)
{
    f3(make_unique<int[]>(n), m);    // bad: pass ownership and size separately
}
```

**Example.** We need to pass the pointer and the number of elements as an
integral object:

```cpp
extern void f4(vector<int>&);   // separately compiled, possibly dynamically loaded
extern void f4(span<int>);      // separately compiled, possibly dynamically loaded
                                // NB: this assumes the calling code is ABI-compatible, using a
                                // compatible C++ compiler and the same stdlib implementation

void g3(int n)
{
    vector<int> v(n);
    f4(v);                     // pass a reference, retain ownership
    f4(span<int>{v});          // pass a view, retain ownership
}
```

This design carries the number of elements along as an integral part of an
object, so that errors are unlikely and dynamic (run-time) checking is always
feasible, if not always affordable.

**Example.** How do we transfer both ownership and all information needed for
validating use?

```cpp
vector<int> f5(int n)    // OK: move
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

owner<int*> f7(int n)    // bad: loses n and we might forget to delete
{
    owner<int*> p = new int[n];
    // ... initialize *p ...
    return p;
}
```

**Example.** `???` — show how possible checks are avoided by interfaces that
pass polymorphic base classes around, when they actually know what they need? Or
strings as "free-style" options.

**Enforcement.**

- Flag (pointer, count)-style interfaces (this will flag a lot of examples that
  can't be fixed for compatibility reasons)
- `???`

## Rationale

Adopted as written, with the qualification the rule itself supplies: it expects
to flag interfaces "that can't be fixed for compatibility reasons". Every Win32
call in this codebase is such an interface. The rule therefore applies to *our*
functions, not to the boundary with the operating system, and the boundary is
where those shapes are supposed to stop — which is the same argument as
[0002](0002-keep-core-free-of-ide-knowledge.md).

The `???` markers are transcribed rather than tidied away: they mark where the
upstream document is unfinished, and a reader is entitled to know that.

## Consequences

### Positive

- Gives a precise reason to convert internal buffer functions to `std::span`,
  rather than "spans are nicer".
- Draws the line in the right place: the Win32 shape is tolerated at the call,
  not propagated inward.

### Negative

- Following it fully would mean a `std::span` conversion pass over `Fs` and
  `Text`, which is real work with no visible behaviour change, and therefore
  work that will keep being deferred.

### Neutral

- The rule's ABI cautions do not apply here; everything is statically linked
  into one executable.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| Raw buffer plus length passed between internal functions | `Core/src/Core/Fs/DirectoryWatcher.cpp` (the `ReadDirectoryChangesW` buffer walk) | The Win32 call itself is exempt; the internal walk over the result is not |
| `std::span` available but unused | `Core/src/corepch.h:13` | Same item as in [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) |

## Related

- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the
  previous rule.
- [0013](0013-p-7-catch-run-time-errors-early.md) — the next rule.
- [0002](0002-keep-core-free-of-ide-knowledge.md) — where platform shapes are
  supposed to stop.
