# 0009. P.3: Express intent

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Unless the intent of some code is stated — in names, in types, or in comments —
it is impossible to tell whether the code does what it is supposed to do. A
reviewer cannot find a bug in code whose purpose they have to reconstruct from
its mechanism.

The upstream example is a loop:

```cpp
gsl::index i = 0;
while (i < v.size()) {
    // ... do something with v[i] ...
}
```

The intent of "just" looping over the elements of `v` is not expressed. The
implementation detail of an index is exposed, so that it might be misused, and
`i` outlives the scope of the loop, which might or might not be intended. The
reader cannot know from just this section of code.

Better:

```cpp
for (const auto& x : v) { /* do something with the value of x */ }
```

Now there is no explicit mention of the iteration mechanism, and the loop
operates on a reference to `const` elements, so accidental modification cannot
happen. If modification is desired, say so:

```cpp
for (auto& x : v) { /* modify x */ }
```

Sometimes better still, use a named algorithm, which directly expresses the
intent:

```cpp
for_each(v, [](int x) { /* do something with the value of x */ });
for_each(par, v, [](int x) { /* do something with the value of x */ });
```

The last variant makes it clear that the order in which the elements of `v` are
handled is not of interest.

Some language constructs express intent better than others. If two `int`s are
meant to be the coordinates of a 2D point, say so:

```cpp
draw_line(int, int, int, int);  // obscure
draw_line(Point, Point);        // clearer
```

An alternative formulation of the whole rule: say what should be done, rather
than just how it should be done.

## Decision

Express intent. Prefer the construct that states the purpose over the construct
that states the mechanism.

In practice:

- Iterate with a range-`for` over a container rather than with an index, unless
  the index itself is part of the meaning.
- Bind loop variables by reference to `const` unless the loop modifies them; the
  mutable form is a statement that mutation is intended.
- Keep a loop variable's scope no larger than the loop.
- Reach for a named algorithm when one says what the loop is for.
- Give a group of values that means one thing one type that means that thing,
  rather than passing the parts.

A programmer working under this rule is expected to be familiar with the
guidelines support library, the ISO C++ standard library, and whatever
foundation libraries the project at hand uses.

## Rationale

This is [0007](0007-p-1-express-ideas-directly-in-code.md) applied to control
flow and to parameter lists, and it earns a separate record because the failures
look different. P.1 is about a declaration that lies or says too little; P.3 is
about code that is correct but unreadable, where the mechanism has crowded out
the purpose. The second kind survives review — nobody objects to a `while` loop
— and then costs on every later read.

The scope point is not decoration. A loop variable that outlives its loop is a
name that a later edit can pick up by accident, and the compiler will not
complain.

## Consequences

### Positive

- The purpose of a piece of code is legible without simulating it.
- Constness in the loop head removes accidental modification as a possibility
  rather than as a convention.
- Narrower scopes mean fewer names in flight and fewer accidental reuses.

### Negative

- A named algorithm is only clearer to a reader who knows it; `for_each` with a
  lambda can read as ceremony where the loop read as plain.
- Grouping parameters into a type is another type to define, name, and include,
  and small structs proliferate.
- Some loops genuinely need the index, and a rule stated this way invites
  contorting them to avoid it.

### Neutral

- Which algorithm library is the default vocabulary is settled by
  [0019](0019-p-13-use-support-libraries-as-appropriate.md), not here.
- The parallel form (`for_each(par, ...)`) is mentioned upstream as an intent
  marker; adopting parallel algorithms is a separate decision.

## Enforcement

Look for common patterns for which there are better alternatives:

- simple `for` loops vs. range-`for` loops
- `f(T*, int)` interfaces vs. `f(span<T>)` interfaces
- loop variables in too large a scope
- naked `new` and `delete`
- functions with many parameters of built-in types

There is a huge scope for cleverness and semi-automated program transformation.

## Related

- [0007](0007-p-1-express-ideas-directly-in-code.md) — the general rule this
  specialises.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — why
  `span` beats a pointer and a count.
- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — why the `const`
  form of the loop is the default one.
