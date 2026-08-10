# 0155. R.12: Immediately give the result of an explicit resource allocation to a manager object

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

If an allocated resource is not handed to an owner immediately, an exception or
a return in between can leak it.

```cpp
void f(const string& name)
{
    FILE* f = fopen(name, "r");            // open the file
    vector<char> buf(1024);
    auto _ = finally([f] { fclose(f); });  // remember to close the file
    // ...
}
```

This looks safe, and is not. The allocation of `buf` might fail, and the scope
guard that would have closed the file has not been created yet, so the file
handle leaks. The two statements are in the wrong order, and nothing about the
code says so.

Using a type that owns the handle from the moment it exists removes the window
entirely:

```cpp
void f(const string& name)
{
    ifstream f{name};   // open the file
    vector<char> buf(1024);
    // ...
}
```

The use of the file handle inside `ifstream` is simple, efficient and safe.

"Immediately" means in the same expression that performs the acquisition, not
merely soon afterwards. Any statement between the acquisition and the handoff is
a statement that can exit the function.

## Decision

Never let an acquired resource exist outside an owner, even briefly.

- Acquire and take ownership in one expression: `ifstream f{name}`, `auto p =
  make_unique<T>(...)`, `lock_guard g{m}`.
- Where an acquisition returns a raw handle from a C API, construct the wrapper
  from it in the same statement, with nothing between the call and the
  construction.
- Do not use a scope guard registered on a later line to release something
  acquired on an earlier one; that gap is the bug this rule is about.
- Prefer an existing owning type — a stream, a container, a smart pointer — to
  writing the acquisition and the handoff yourself.

## Rationale

The interesting part of this rule is that the bad example already tried to be
correct. It uses a scope guard, it releases on every path after the guard is
built, and it still leaks, because the ordering hazard is in the gap rather than
in the cleanup. That is a failure discipline cannot fix: no reviewer reads
`vector<char> buf(1024)` as a throw site. Collapsing acquisition and ownership
into a single expression removes the gap rather than making the code inside it
safer.

## Consequences

### Positive

- There is no program point at which an unowned resource exists, so no
  intervening operation can leak it.
- Ordering of local declarations stops being a correctness question.
- The resulting code is shorter than the version with the explicit release.

### Negative

- Not every resource has a ready-made owning type, so the rule often implies
  writing one before you can obey it, for what was a single call.
- "Immediately" is not a mechanically checkable property, and the guidelines'
  own enforcement note concedes that recognising which calls perform a direct
  resource allocation is the hard part.
- Constructing a wrapper in the same expression as the acquisition means the
  wrapper's constructor must accept an already-acquired handle, which is an
  extra constructor and an extra invalid state to think about.
- The rule is usually motivated by exceptions, but it is not only about them: an
  early return, a `goto`, or a second failing acquisition in the gap leaks just
  as well — which is easiest to miss in code that reports errors by return value
  and therefore looks as though it has no exit points to worry about.

### Neutral

- Whether the owner is a standard type or a hand-written one does not matter to
  this rule, only that it exists at the moment of acquisition.
- This record is about the moment of acquisition; how many acquisitions may
  appear in one statement is the next rule.

## Enforcement

- Flag explicit allocations used to initialise raw pointers. The practical
  difficulty is recognising how many direct resource allocations there are.

## Related

- [0147](0147-r-1-manage-resources-automatically-using-resource-handles-and-raii-resource-acquisition-is-initialization.md)
  — the owner this rule requires you to hand the resource to.
- [0156](0156-r-13-perform-at-most-one-explicit-resource-allocation-in-a-single-expression-statement.md)
  — the companion rule about the expression itself.
- [0154](0154-r-11-avoid-calling-new-and-delete-explicitly.md) — avoiding the
  explicit allocation is better still.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the leak being prevented.
