# 0079. C.65: Make move assignment safe for self-assignment

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If `x = x` changes the value of `x`, people will be surprised and bad errors can
occur. People do not usually write a self-assignment that turns into a move
directly, but it can occur — and `std::swap` is implemented using move
operations, so if you accidentally do `swap(a, b)` where `a` and `b` refer to
the same object, failing to handle self-move could be a serious and subtle
error.

```cpp
class Foo {
    string s;
    int i;
public:
    Foo& operator=(Foo&& a);
    // ...
};

Foo& Foo::operator=(Foo&& a) noexcept  // OK, but there is a cost
{
    if (this == &a) return *this;  // this line is redundant
    s = std::move(a.s);
    i = a.i;
    return *this;
}
```

The one-in-a-million argument against `if (this == &a) return *this;` tests from
the discussion of copy self-assignment is even more relevant for self-move.

There is no known general way of avoiding an `if (this == &a) return *this;`
test for a move assignment and still getting a correct answer — that is, after
`x = x` the value of `x` is unchanged.

The ISO standard guarantees only a "valid but unspecified" state for the
standard-library containers. Apparently this has not been a problem in about ten
years of experimental and production use. The rule here is more cautious and
insists on complete safety.

For a pointer member, there is a way to move without a test — imagine this as
code inside a move assignment:

```cpp
// move from other.ptr to this->ptr
T* temp = other.ptr;
other.ptr = nullptr;
delete ptr;
ptr = temp;
```

## Decision

Make `x = std::move(x)` leave `x` with its value intact, or at minimum in a
specified valid state.

- Where the members are themselves self-move-safe, let the compiler-generated
  move assignment inherit the property and write nothing.
- For a raw resource member, use the steal-then-release ordering shown above:
  take the source's pointer into a temporary, null the source, release the
  target's old resource, then install the temporary. Self-move then leaves the
  object holding its own pointer.
- Where no such ordering exists, write the explicit `if (this == &a) return
  *this;` guard. Unlike in copy assignment, this is the accepted answer, not a
  smell.
- Never release the target's resource before the source's has been secured, and
  never leave a self-moved object holding a deleted or null pointer.
- Remember that `std::swap` is built from moves, so `swap(a, a)` exercises this
  path even where no self-assignment appears in the source.

## Rationale

Self-move differs from self-copy in that the naive implementation destroys the
object rather than merely wasting work: releasing the target's resource and then
stealing from the source means stealing from something already freed. That is a
use-after-free, not a wrong value.

The guideline is explicit that no general test-free technique exists, which is
why the guard that
[0076](0076-c-62-make-copy-assignment-safe-for-self-assignment.md) discourages is
acceptable here. The two records disagree about the same line of code for a
reason, and it is worth knowing which one you are reading.

## Consequences

### Positive

- `swap(a, a)` and aliased assignments cannot destroy the object.
- The steal-then-release ordering costs nothing in the ordinary case and is
  correct in the aliased one.
- The rule is stronger than the standard's guarantee, so types written to it
  compose with looser code safely rather than the other way round.

### Negative

- The guard is a branch on every move assignment, paid a million times for the
  one case that needs it — the guideline itself calls the line redundant in the
  example where the members are already safe.
- It is one more thing that must be repeated in every hand-written move
  assignment, and omitting it is invisible until an aliased call occurs.
- Insisting on complete safety goes beyond what the standard library provides
  for its own types, so the codebase holds itself to a rule its dependencies do
  not follow, and cannot rely on the property when using them.

### Neutral

- The advice is the opposite of the copy-assignment advice on the same line of
  code; the difference is that copy assignment has copy-and-swap and move
  assignment has no equivalent.
- Standard-library container and string members can be treated as safe for
  ordinary, non-life-critical uses.

## Enforcement

- (Moderate) In the case of self-assignment, a move assignment operator should
  not leave the object holding pointer members that have been deleted or set to
  null.
- (Not enforceable) Look at the use of standard-library container types,
  including `string`, and consider them safe for ordinary, non-life-critical
  uses.

## Related

- [0076](0076-c-62-make-copy-assignment-safe-for-self-assignment.md) — the copy
  case, which reaches the opposite conclusion about the explicit test.
- [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md)
  — the general obligation this record specialises to the aliased case.
- [0086](0086-c-84-a-swap-function-must-not-fail.md) — `swap` is built from
  moves, which is how self-move gets reached.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the harm from getting the
  release ordering wrong.
