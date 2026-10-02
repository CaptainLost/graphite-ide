# 0172. ES.1: Prefer the standard library to other libraries and to "handcrafted code"

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Code using a library can be much easier to write than code working directly with
language features. It tends to be much shorter, sits at a higher level of
abstraction, and the library code is presumably already tested. The ISO C++
standard library is among the most widely known and best tested libraries in
existence, and it is available as part of every C++ implementation.

```cpp
auto sum = accumulate(begin(a), end(a), 0.0);   // good
```

A range version of `accumulate` would be better still, because it names the
whole sequence rather than a pair of iterators that could disagree:

```cpp
auto sum = accumulate(v, 0.0); // better
```

What should not happen is hand-coding a well-known algorithm:

```cpp
int max = v.size();   // bad: verbose, purpose unstated
double sum = 0.0;
for (int i = 0; i < max; ++i)
    sum = sum + v[i];
```

The loop is four lines instead of one, it states its mechanism rather than its
purpose, and the reader has to run it in their head to discover that it is a
sum. It also silently narrows `v.size()` into an `int`.

The exception is scale-related. Large parts of the standard library rely on
dynamic allocation from the free store — notably the containers, though not the
algorithms — which makes them unsuitable for some hard real-time and embedded
applications. In such cases the answer is to provide or use similar facilities,
for example a standard-library-style container implemented over a pool
allocator, rather than to abandon the library style altogether.

## Decision

Reach for the standard library before writing a loop, and before adopting a
third-party equivalent of something the standard library already provides.

- Express a traversal as a standard algorithm, and prefer the range overload to
  the iterator-pair overload where one exists.
- Use the standard containers, `std::string`, `std::string_view`, `std::span`,
  `std::optional` and the standard smart pointers rather than hand-rolled
  substitutes.
- Where a third-party library is genuinely needed, prefer one that composes with
  standard types over one that defines its own parallel vocabulary.
- Where allocation behaviour is the problem, keep the standard-library
  *interface* and change the allocator or the storage, rather than dropping to
  raw loops and raw arrays.

## Rationale

The strongest argument is not brevity, it is testedness. A hand-written loop is
a fresh, untested implementation of a solved problem, written under deadline, by
one person, and reviewed by nobody who was looking for off-by-one errors. The
standard library's version has been compiled by every C++ programmer alive.
Brevity is the visible benefit; correctness by delegation is the real one.

## Consequences

### Positive

- Less code to write, read, review and maintain, and the code that remains says
  what it means rather than how it is done.
- Bugs in a standard algorithm are found and fixed by the implementation's
  maintainers, not by the code that calls it.
- Standard vocabulary types compose with each other and with third-party code
  that also uses them.

### Negative

- Algorithm calls with lambdas can be harder to step through in a debugger than
  a plain loop, and the diagnostics from a misused template are worse than the
  diagnostics from a misused `for`.
- Some standard facilities carry costs a hand-written loop would not: allocation
  in containers, and abstraction that a debug build does not optimise away.
- "Prefer the standard library" is not a bright line. Nothing here says at what
  point a merely adequate standard facility should lose to a purpose-built one,
  so the rule is applied by judgement.
- A third-party library that defines its own container and string types forms a
  boundary at which standard vocabulary types stop, and conversions accumulate
  there. Every such dependency adds another such boundary.

### Neutral

- The hard real-time and embedded exception is explicitly permitted upstream,
  and it turns on allocation behaviour rather than on the library's style.
- The rule is about preference, not prohibition. A loop that genuinely does not
  correspond to any algorithm stays a loop.

## Enforcement

Not easy to check mechanically. Look for messy loops, nested loops, long
functions, an absence of function calls, and a lack of use of non-built-in
types. High cyclomatic complexity in a function that only walks a sequence is a
signal.

## Related

- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the philosophy
  rule this one specialises to the standard library.
- [0173](0173-es-2-prefer-suitable-abstractions-to-direct-use-of-language-features.md)
  — the same argument stated for abstractions generally.
- [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) — reimplementing
  a standard algorithm is a form of repetition.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — the exception here is where
  the library's costs stop being acceptable.
