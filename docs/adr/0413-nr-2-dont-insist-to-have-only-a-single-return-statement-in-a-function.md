# 0413. NR.2: Don't insist to have only a single return-statement in a function

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section NR (Non-Rules and myths), transcribed 2026-08-10

## Context

The NR section collects rules that are popular somewhere but that the
guidelines deliberately do not recommend. This record adopts the rejection, not
the practice being rejected.

The practice here is the single-exit convention: a function must contain exactly
one `return`. It leads to unnecessarily convoluted code and to the introduction
of extra state variables, and in particular it makes it harder to concentrate
error checking at the top of a function.

```cpp
template<class T>
//  requires Number<T>
string sign(T x)
{
    if (x < 0)
        return "negative";
    if (x > 0)
        return "positive";
    return "zero";
}
```

To use a single return only, we would have to do something like:

```cpp
template<class T>
//  requires Number<T>
string sign(T x)        // bad
{
    string res;
    if (x < 0)
        res = "negative";
    else if (x > 0)
        res = "positive";
    else
        res = "zero";
    return res;
}
```

This is both longer and likely to be less efficient. The larger and more
complicated the function, the more painful the workarounds get. Many simple
functions will naturally have just one return, because of their simpler inherent
logic; that is not the same as insisting on it.

The guard-clause case is worse:

```cpp
int index(const char* p)
{
    if (!p) return -1;  // error indicator: alternatively "throw nullptr_error{}"
    // ... do a lookup to find the index for p
    return i;
}
```

Applying the rule gives:

```cpp
int index2(const char* p)
{
    int i;
    if (!p)
        i = -1;  // error indicator
    else {
        // ... do a lookup to find the index for p
    }
    return i;
}
```

This deliberately violates the rule against uninitialised variables, because the
style commonly leads to that, and it is also a temptation to use the "all
cleanup at the end, `goto exit`" non-rule.

## Decision

Do not impose a single-return convention, and do not follow one.

- Return as soon as the answer is known. A function may contain as many
  `return` statements as its logic calls for.
- Use guard clauses at the top of a function to reject invalid or trivial cases,
  so that the body below them can assume the normal case.
- Do not introduce a result variable whose only purpose is to be assigned in
  every branch and returned once.
- Keep the function short enough that multiple exits remain easy to see; if the
  exits have become hard to track, the fix is to split the function, not to
  merge the returns.
- Where cleanup is needed on every path, use a destructor or a scope-guard
  object rather than routing all paths through a single exit.

## Rationale

The single-exit rule solves a problem C++ does not have. It comes from
languages without destructors, where a function had to funnel through one place
so that the release of resources could be written there once. RAII performs that
funnelling in the type system, on every path including a thrown exception, so
the convention's only remaining effect is the damage: an extra variable, a
longer function, and a window in which that variable is uninitialised.

The `index2` example is the argument in miniature. Obeying the rule costs an
uninitialised `int`, and it is only a matter of size before it also costs a
`goto`.

## Consequences

### Positive

- Guard clauses keep error checking at the top, so the main path is not nested
  inside the checks.
- No result variable means no interval in which the result is declared but not
  yet decided.
- Returning a value directly permits copy elision, which an assign-then-return
  variable can obstruct.

### Negative

- Multiple exits genuinely do make a long function harder to audit; the rule
  relies on functions being short, which is a separate discipline that may not
  hold.
- Anything that must happen on every path now depends on RAII being used
  correctly. Where a resource is not held by an object, an early return is a
  leak, and the compiler will not say so.
- Setting a breakpoint on "the function returns" becomes several breakpoints.
- Some established coding standards mandate single exit, so this rule puts a
  codebase in conflict with them rather than resolving the argument.

### Neutral

- A function that naturally has one return is not made worse by having one. The
  objection is to the insistence.
- Throwing is another exit, and one the single-return convention cannot control
  at all; a function that calls anything which can throw already has more than
  one way out.

## Related

- [0440](0440-f-3-keep-functions-short-and-simple.md) — the alternative upstream
  offers, and the discipline multiple exits depend on.
- [0417](0417-nr-6-dont-place-all-cleanup-actions-at-the-end-of-a-function-and-goto-exit.md)
  — the non-rule that single-exit leads to.
- [0183](0183-es-20-always-initialize-an-object.md) — the rule the single-exit
  workaround breaks.
- [0474](0474-f-56-avoid-unnecessary-condition-nesting.md) — guard clauses are
  how nesting is avoided.
