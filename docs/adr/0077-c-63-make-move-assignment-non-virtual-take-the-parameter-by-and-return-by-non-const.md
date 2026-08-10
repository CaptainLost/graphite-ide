# 0077. C.63: Make move assignment non-virtual, take the parameter by &&, and return by non-const&

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The conventional move-assignment signature is simple and efficient, for exactly
the reasons the copy-assignment rule gives:

```cpp
class Foo {
public:
    Foo& operator=(Foo&& x) noexcept;   // GOOD: the conventional shape
    Foo& operator=(const Foo& x);       // its copy counterpart
    // ...
};
```

The parameter is a non-const rvalue reference, because a move must be able to
modify its source in order to steal from it — a `const&&` parameter would make
the operation impossible to implement as a move. The return type is `T&`, the
same as for copy assignment, so that assignment chains and so that the type
satisfies the standard library's container requirements. And the operator is not
virtual, for the same reason copy assignment is not: assignment through a base
reference in a hierarchy has no correct meaning.

## Decision

Declare move assignment as `T& operator=(T&&) noexcept`, non-virtual.

- Take the parameter by non-const rvalue reference, return `T&`, and return
  `*this`.
- Do not declare it virtual; if a hierarchy needs polymorphic assignment, give
  it a named function rather than an operator.
- Mark it `noexcept`, per
  [0080](0080-c-66-make-move-operations-noexcept.md).
- Ensure it assigns every base and every member, implicitly or explicitly, using
  their move assignments and not their copy assignments.
- Provide it alongside the copy-assignment overload rather than in place of it,
  so that lvalue and rvalue sources each get the right operation.

## Rationale

Move assignment exists to be selected by overload resolution against the copy
overload, and it only pays for itself if it can actually steal. A parameter
declared `const T&&` still counts as a move assignment operator to the language,
but it cannot modify its source, so every "move" it performs degrades to a copy:
the type appears to have the optimisation and does not have it.

That is the sharp point. Getting the return type wrong fails loudly —
`std::assignable_from` and the library's *Cpp17MoveAssignable* requirement both
demand `T&`, so generic code stops compiling — but getting the parameter's
constness wrong fails silently, and silence is the expensive failure.

## Consequences

### Positive

- The type moves where the library expects it to move, including on vector
  reallocation.
- Assignment chains and generic code that assumes `T&` continue to work.
- The rvalue overload absorbs the temporaries, so the copy overload can stay
  simple.

### Negative

- Declaring move assignment suppresses the implicit copy constructor and
  requires the whole set of special members to be considered explicitly, which
  is more declarations than the class had before.
- Writing both overloads duplicates the assignment logic; the by-value
  "unified assignment" idiom avoids that but does not have this signature, so
  the rule closes off a legitimate alternative.
- Non-virtual assignment leaves a hierarchy with no operator-based way to
  assign, so the named-function workaround has to be invented and documented per
  type.

### Neutral

- What a move must actually do to its source is
  [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md);
  this record only fixes the shape.
- The rule mirrors
  [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md)
  deliberately, so the two operators read the same way.

## Enforcement

- (Simple) An assignment operator should not be virtual. Here be dragons.
- (Simple) An assignment operator should return `T&` to enable chaining, not
  alternatives like `const T&`, which interfere with composability and with
  putting objects in containers.
- (Moderate) A move assignment operator should, implicitly or explicitly, invoke
  all base and member move assignment operators.

## Related

- [0074](0074-c-60-make-copy-assignment-non-virtual-take-the-parameter-by-const-and-return-by-non-const.md)
  — the copy-assignment rule this one is the counterpart of.
- [0078](0078-c-64-a-move-operation-should-move-and-leave-its-source-in-a-valid-state.md)
  — the semantics required of the operator declared this way.
- [0080](0080-c-66-make-move-operations-noexcept.md) — why the `noexcept` in the
  signature is not optional.
- [0079](0079-c-65-make-move-assignment-safe-for-self-assignment.md) — the
  self-assignment obligation that comes with it.
