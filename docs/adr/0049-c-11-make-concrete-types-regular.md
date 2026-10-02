# 0049. C.11: Make concrete types regular

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Regular types are easier to understand and reason about than types that are not
regular; irregularities require extra effort to understand and to use. The
built-in types are regular, and so are standard library classes such as
`string`, `vector` and `map`. Concrete classes without assignment and equality
can be defined, but they are, and should be, rare.

```cpp
struct Bundle {
    string name;
    vector<Record> vr;
};

bool operator==(const Bundle& a, const Bundle& b)
{
    return a.name == b.name && a.vr == b.vr;
}

Bundle b1 { "my bundle", {r1, r2, r3}};
Bundle b2 = b1;
if (!(b1 == b2)) error("impossible!");
b2.name = "the other bundle";
if (b1 == b2) error("No!");
```

In particular, if a concrete type is copyable, prefer to give it an equality
comparison operator as well, and ensure that `a = b` implies `a == b`. That
implication is the whole content of the rule: copying and comparison must agree
about what the value of an object is.

There are two acknowledged limits. For structs intended to be shared with C
code, defining `operator==` may not be feasible. And handles for resources that
cannot be cloned — a `scoped_lock` on a mutex, for instance — are concrete types
that typically cannot be copied; they can usually be moved, so they cannot be
regular and tend to be move-only instead.

## Decision

Give a concrete value type the full set of value operations, and make them
agree.

- Provide default construction, copy, move, assignment, destruction and
  equality, and let the compiler generate them wherever it can, per
  [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md).
- Ensure `a = b` implies `a == b`. If a member is excluded from comparison, it
  must also be excluded from copying, or the two operations disagree about the
  object's value.
- Where the members are comparable, prefer a defaulted `operator==` to a
  hand-written one, so that adding a member cannot silently leave comparison
  behind.
- Do not force regularity onto a resource handle. A type that owns something
  uncopyable is move-only by nature, and that is not a defect.
- Do not add `operator==` to a type that exists to be shared with C code if
  doing so would break its layout guarantees or its usability from C.

## Rationale

Regularity is what lets a type be used with code that was not written for it.
Every standard algorithm and container states its requirements in terms of
copyability, assignability and comparability, so an irregular type is not merely
unusual, it is excluded from the library and from any generic code the
repository writes.

The `a = b` implies `a == b` implication is worth more than the list of
operations. A type can have all six special members and an `operator==` and
still be a trap, if comparison ignores a member that copying carries; the
implication is the property that makes the operations mean something together
rather than separately.

## Consequences

### Positive

- The type works with standard containers, algorithms and any generic code
  written against the usual concepts.
- Tests can compare results directly rather than field by field.
- The reader can predict what `=`, `==` and copy-construction do without reading
  them.

### Negative

- Equality is not always well defined. A type holding a cache, a memoised value,
  a timestamp or a `std::function` has members whose contribution to "the same
  value" is a judgement, and defaulted comparison will get it wrong silently.
- A defaulted `operator==` compares every member, which for a large type is real
  work in a place callers do not expect it, and it is easy to write an O(n)
  comparison into a hot loop.
- Regularity conflicts with resource ownership: the more a type owns, the less
  sensible copying it is, and the rule has to yield entirely for handles.

### Neutral

- Move-only types are outside the rule rather than in violation of it.
- C-compatible structs are an acknowledged exception on comparison.

## Related

- [0048](0048-c-10-prefer-concrete-types-over-class-hierarchies.md) — the
  concrete types this record then asks to be regular.
- [0064](0064-c-43-ensure-that-a-copyable-class-has-a-default-constructor.md) —
  the semiregular half of the same requirement.
- [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) — the
  cheapest way to get regularity is not to write it.
- [0053](0053-c-22-make-default-operations-consistent.md) — operations that
  disagree with one another are the failure mode this record names.
