# 0080. C.66: Make move operations noexcept

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A throwing move violates most people's reasonable assumptions, and a
non-throwing move will be used more efficiently by standard-library and language
facilities.

A move that only shuffles pointers has nothing in it that can fail:

```cpp
template<typename T>
class Vector {
public:
    Vector(Vector&& a) noexcept :elem{a.elem}, sz{a.sz} { a.sz = 0; a.elem = nullptr; }
    Vector& operator=(Vector&& a) noexcept { elem = a.elem; sz = a.sz; a.sz = 0; a.elem = nullptr; }
    // ...
private:
    T* elem;
    int sz;
};
```

A move that delegates to the copy does:

```cpp
template<typename T>
class Vector2 {
public:
    Vector2(Vector2&& a) { *this = a; }             // just use the copy
    Vector2& operator=(Vector2&& a) { *this = a; }  // just use the copy
    // ...
private:
    T* elem;
    int sz;
};
```

This `Vector2` is not just inefficient: since a vector copy requires allocation,
it can throw.

## Decision

Mark every move constructor and move assignment operator `noexcept`, and
implement them so the mark is true.

- Write `noexcept` on both move operations of every type that declares them.
- Implement a move as a transfer of the existing representation — steal the
  pointers, null the source — never as a copy in disguise.
- Do not allocate, and do not call anything that can fail, inside a move
  operation.
- If a type's move genuinely cannot be made non-throwing, leave it without move
  operations rather than declaring throwing ones, and record why in a comment on
  the class.
- Where moves are defaulted, check that the members are themselves
  `noexcept`-movable, since the generated operation's specification is derived
  from theirs.

## Rationale

`std::vector` reallocation queries `std::is_nothrow_move_constructible` and
copies instead of moving when the answer is no, because it cannot restore the
old buffer if a move fails partway through. So a missing `noexcept` does not
merely forgo an optimisation; it silently converts every reallocation of a
vector of that type into a deep copy, and no diagnostic is issued.

The `Vector2` example shows the two failures are the same failure: a move
implemented as a copy is both the reason it can throw and the reason it is slow.

## Consequences

### Positive

- Container reallocation, `std::swap`, and the standard algorithms take the
  move path instead of falling back to copying.
- The strong guarantee becomes achievable in operations built on move, because
  the move step cannot fail.
- The compiler emits no unwinding machinery around the operation.

### Negative

- `noexcept` is a promise enforced by `std::terminate`: a move that throws
  despite the mark kills the process rather than propagating, which is a harsh
  failure mode for a wrong annotation.
- It constrains the implementation permanently — a later change that adds an
  allocation to the move, perhaps by adding a member, breaks the promise
  silently unless the annotation is computed rather than written.
- Types whose representation cannot be transferred without allocating, such as
  those with a non-propagating allocator or an inline buffer, cannot satisfy the
  rule and are pushed towards having no move operations at all.

### Neutral

- The annotation is consumed by the standard library's traits rather than by any
  handler, so it must be written even on a move that manifestly cannot throw;
  the traits are driven by what is declared, not by what the body does.
- The rule applies to both move operations equally; declaring one `noexcept` and
  not the other is a bug.

## Enforcement

- (Simple) A move operation should be marked `noexcept`.

## Related

- [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md)
  — the steal-and-null implementation that makes `noexcept` truthful.
- [0065](0065-c-44-prefer-default-constructors-to-be-simple-and-non-throwing.md)
  — the same non-throwing argument for the default constructor.
- [0087](0087-c-85-make-swap-noexcept.md) — the same annotation for `swap`, which
  is built from moves.
- [0077](0077-c-63-make-move-assignment-non-virtual-take-the-parameter-by-and-return-by-non-const.md)
  — the signature this annotation belongs on.
