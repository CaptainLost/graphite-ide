# 0075. C.61: A copy operation should copy

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

That is the generally assumed semantics: after `x = y`, we should have `x == y`.
After a copy, `x` and `y` can be independent objects — value semantics, the way
non-pointer built-in types and the standard-library types work — or they can
refer to a shared object, which is pointer semantics, the way pointers work.

Value semantics: the copy has its own buffer, so modifying one object does not
change the other.

```cpp
class X {   // OK: value semantics
public:
    X();
    X(const X&);     // copy X
    void modify();   // change the value of X
    // ...
    ~X() { delete[] p; }
private:
    T* p;
    int sz;
};

bool operator==(const X& a, const X& b)
{
    return a.sz == b.sz && equal(a.p, a.p + a.sz, b.p, b.p + b.sz);
}

X::X(const X& a)
    :p{new T[a.sz]}, sz{a.sz}
{
    copy(a.p, a.p + sz, p);
}

X x;
X y = x;
if (x != y) throw Bad{};
x.modify();
if (x == y) throw Bad{};   // assume value semantics
```

Pointer semantics: the copy shares the buffer, comparison compares the pointer,
and modifying through one object is visible through the other.

```cpp
class X2 {  // OK: pointer semantics
public:
    X2();
    X2(const X2&) = default; // shallow copy
    ~X2() = default;
    void modify();          // change the pointed-to value
    // ...
private:
    T* p;
    int sz;
};

bool operator==(const X2& a, const X2& b)
{
    return a.sz == b.sz && a.p == b.p;
}

X2 x;
X2 y = x;
if (x != y) throw Bad{};
x.modify();
if (x != y) throw Bad{};  // assume pointer semantics
```

Prefer value semantics unless you are building a smart pointer. Value semantics
is the simplest to reason about and what the standard-library facilities expect.

## Decision

Make a copy operation produce an object that compares equal to its source, and
keep the copy consistent with the type's comparison and its destructor.

- After `X y = x;` or `x = y;`, `x == y` must hold.
- Choose value semantics or pointer semantics for the type, state which in a
  comment on the class, and make the copy operations, `operator==`, and the
  destructor all agree with the choice.
- Prefer value semantics; use pointer semantics only when the type is a handle
  or a smart pointer whose whole purpose is sharing.
- Do not write a copy operation that copies only part of the object, adjusts a
  value on the way, or has a side effect the source does not have.

## Rationale

Copying is not a private matter between a type and its author: every algorithm,
container, and generic function in the standard library assumes `x == y` after a
copy, and none of them can check it. A type that copies partially will work in
testing and fail inside `std::sort` or `std::vector` reallocation, at a distance
from the definition that caused it.

The value-versus-pointer choice is only a real choice if it is made once and
applied to all three of copy, comparison, and destruction. `X2`'s shallow copy
is correct precisely because its `operator==` compares pointers and its
destructor deletes nothing.

## Consequences

### Positive

- Generic code and standard containers behave as their documentation says.
- The type's identity, equality, and lifetime rules are decided together rather
  than accumulating.
- A reader can predict what a copy costs and what it shares from one comment on
  the class.

### Negative

- Value semantics means a deep copy, which for a large object is expensive and
  cannot be avoided without breaking the rule; the escape is to move rather than
  copy, which is a different operation.
- Pointer semantics keeps copies cheap but makes lifetime everyone's problem —
  `X2` above has no owner and its `= default` destructor leaks unless something
  else deletes the buffer.
- `x == y` after a copy is not checkable by any tool, so the rule rests entirely
  on the author, and a type without an `operator==` cannot even express it.

### Neutral

- Types that are neither values nor handles — those with no meaningful copy at
  all — should suppress copying rather than defining a partial one, per
  [0083](0083-c-81-use-delete-when-you-want-to-disable-default-behavior-without-wanting-an-alternative.md).
- The examples throw to signal a violated expectation, but what they illustrate
  is the meaning of a copy, not how a failing copy should report.

## Enforcement

- (Not enforceable)

## Related

- [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md)
  — the signature this record gives the meaning of.
- [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md)
  — the parallel obligation for moves.
- [0088](0088-c-86-make-symmetric-with-respect-to-operand-types-and-noexcept.md)
  — the `==` that this record's postcondition is stated in terms of.
- [0081](0081-c-67-a-polymorphic-class-should-suppress-public-copy-move.md) — a
  polymorphic class cannot satisfy this rule and should not try.
