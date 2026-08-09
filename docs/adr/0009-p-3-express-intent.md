# 0009. P.3: Express intent

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

P.1 asks that an idea live in the code; P.3 asks that the code say which idea.
It lands hardest on the loop-heavy parsing code in `Sln` and the tree walks in
`Fs`.

## Decision

Adopt P.3 as written. Transcribed in full:

> **Reason.** Unless the intent of some code is stated (e.g., in names or
> comments), it is impossible to tell whether the code does what it is supposed
> to do.

**Example.**

```cpp
gsl::index i = 0;
while (i < v.size()) {
    // ... do something with v[i] ...
}
```

The intent of "just" looping over the elements of `v` is not expressed here. The
implementation detail of an index is exposed (so that it might be misused), and
`i` outlives the scope of the loop, which might or might not be intended. The
reader cannot know from just this section of code.

Better:

```cpp
for (const auto& x : v) { /* do something with the value of x */ }
```

Now, there is no explicit mention of the iteration mechanism, and the loop
operates on a reference to `const` elements so that accidental modification
cannot happen. If modification is desired, say so:

```cpp
for (auto& x : v) { /* modify x */ }
```

For more details about for-statements, see ES.71. Sometimes better still, use a
named algorithm. This example uses the `for_each` from the Ranges TS because it
directly expresses the intent:

```cpp
for_each(v, [](int x) { /* do something with the value of x */ });
for_each(par, v, [](int x) { /* do something with the value of x */ });
```

The last variant makes it clear that we are not interested in the order in which
the elements of `v` are handled.

A programmer should be familiar with

- The guidelines support library
- The ISO C++ Standard Library
- Whatever foundation libraries are used for the current project(s)

**Note.** Alternative formulation: Say what should be done, rather than just how
it should be done.

**Note.** Some language constructs express intent better than others.

**Example.** If two `int`s are meant to be the coordinates of a 2D point, say so:

```cpp
draw_line(int, int, int, int);  // obscure: (x1,y1,x2,y2)? (x,y,h,w)? ...?
                                // need to look up documentation to know

draw_line(Point, Point);        // clearer
```

**Enforcement.** Look for common patterns for which there are better
alternatives:

- simple for loops vs. range-for loops
- `f(T*, int)` interfaces vs. `f(span<T>)` interfaces
- loop variables in too large a scope
- naked `new` and `delete`
- functions with many parameters of built-in types

There is a huge scope for cleverness and semi-automated program transformation.

## Rationale

Adopted as written. Of its enforcement list, three items are checkable by eye in
this codebase and worth watching: index loops that could be range-for, loop
variables outliving their loop, and functions taking several built-in
parameters in a row — the last being the shape that lets a caller swap two
arguments silently.

The GSL references in the rule (`gsl::index`, `span<T>`) are read as
`std::size_t` and `std::span`; this project does not vendor the GSL. See
[0019](0019-p-13-use-support-libraries-as-appropriate.md).

## Consequences

### Positive

- Names a concrete review question — "could this be a range-for or a named
  algorithm?" — instead of a matter of taste.

### Negative

- The rule prefers named algorithms, and this codebase's loops often do two
  things at once (walk and filter, parse and accumulate), where splitting into
  algorithm calls costs more than the index loop it replaces.
- `<algorithm>` calls with iterator pairs are not obviously clearer than the
  loop they replace at this scale; the win the rule assumes is real mostly with
  ranges, which the code does not yet use.

### Neutral

- No count of index-style loops is recorded here. Unlike the brace rule in
  [0006](0006-require-braces-around-every-control-flow-body.md), "could have
  been a range-for" is not mechanically decidable, so an inventory would be a
  guess.

## Related

- [0008](0008-p-2-write-in-iso-standard-c.md) — the previous rule.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — the next
  rule.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — P.1, which this refines.
