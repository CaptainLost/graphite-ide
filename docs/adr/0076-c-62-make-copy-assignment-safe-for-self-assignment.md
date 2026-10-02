# 0076. C.62: Make copy assignment safe for self-assignment

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If `x = x` changes the value of `x`, people will be surprised and bad errors
will occur, often including leaks. The standard-library containers handle
self-assignment elegantly and efficiently:

```cpp
std::vector<int> v = {3, 1, 4, 1, 5, 9};
v = v;
// the value of v is still {3, 1, 4, 1, 5, 9}
```

The default assignment generated from members that handle self-assignment
correctly handles self-assignment:

```cpp
struct Bar {
    vector<pair<int, int>> v;
    map<string, int> m;
    string s;
};

Bar b;
// ...
b = b;   // correct and efficient
```

Self-assignment can be handled by explicitly testing for it, but often it is
faster and more elegant to cope without such a test — for instance by using
swap:

```cpp
class Foo {
    string s;
    int i;
public:
    Foo& operator=(const Foo& a);
    // ...
};

Foo& Foo::operator=(const Foo& a)   // OK, but there is a cost
{
    if (this == &a) return *this;
    s = a.s;
    i = a.i;
    return *this;
}
```

This is obviously safe and apparently efficient. However, what if there is one
self-assignment per million assignments? That is about a million redundant
tests — although since the answer is essentially always the same, the branch
predictor will guess right essentially every time. Consider instead:

```cpp
Foo& Foo::operator=(const Foo& a)   // simpler, and probably much better
{
    s = a.s;
    i = a.i;
    return *this;
}
```

`std::string` is safe for self-assignment and so is `int`. All the cost is
carried by the rare case of self-assignment.

## Decision

Make `x = x` leave `x` unchanged, and prefer to achieve that without a test.

- Build the type out of members that are themselves self-assignment-safe, and
  let the compiler-generated assignment operator inherit the property.
- When writing the operator by hand, prefer copy-and-swap, or member-by-member
  assignment through self-safe members, over an `if (this == &a) return *this;`
  guard.
- Never release a resource before the value that replaces it has been secured;
  that ordering is what makes self-assignment destroy the object.
- Add the explicit self-assignment test only where the implementation genuinely
  cannot be made safe without it, and treat its presence as a signal that the
  implementation should be reconsidered.

## Rationale

The reason to avoid the guard is not the cost of the branch — the guideline
concedes the predictor handles it — but that the guard makes an unsafe body
acceptable. An operator that needs the test is one that would corrupt the object
without it, and that fragility remains, waiting for a refactor to move the test
or an inherited path to bypass it.

Self-assignment is rarely written directly. It arrives through aliasing:
`v[i] = v[j]` with equal indices, or an assignment through two references to the
same object, where nothing at the call site looks suspicious.

## Consequences

### Positive

- `x = x` is correct in the paths where it actually happens, which are aliased
  ones nobody inspected.
- Composing self-safe members makes the property free for the common case of a
  class that just holds members.
- Removing the guard forces the resource-ordering question to be answered rather
  than papered over.

### Negative

- Copy-and-swap buys the property with a full temporary copy, which
  [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md)
  already notes can be an order of magnitude of waste for large objects.
- A hand-written operator that manages a raw resource often cannot be made
  self-safe without either the test or a temporary, so the rule reduces to
  "pick which cost".
- The property is not checkable in general: a test suite that never aliases will
  pass against a broken operator.

### Neutral

- The upstream enforcement bullet is stated as flagging the guard pattern; it is
  a heuristic, since the guard is legitimate in the cases where nothing else
  works.
- Move assignment has the same requirement but a different answer, recorded
  separately in
  [0079](0079-c-65-make-move-assignment-safe-for-self-assignment.md).

## Enforcement

- (Simple) Assignment operators should not contain the pattern
  `if (this == &a) return *this;`.

## Related

- [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md)
  — copy-and-swap, the implementation that makes this rule automatic.
- [0079](0079-c-65-make-move-assignment-safe-for-self-assignment.md) — the same
  requirement for moves, where the guard cannot generally be avoided.
- [0085](0085-c-83-for-value-like-types-consider-providing-a-noexcept-swap-function.md)
  — the swap the preferred implementation needs.
- [0014](0014-p-8-do-not-leak-any-resources.md) — an unsafe self-assignment leaks
  or double-frees, which is the concrete harm.
