# 0051. C.20: If you can avoid defining default operations, do

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Not declaring the default operations is the simplest option and gives the
cleanest semantics. The default operations are the default constructor `X()`,
the copy constructor `X(const X&)`, the copy assignment `operator=(const X&)`,
the move constructor `X(X&&)`, the move assignment `operator=(X&&)`, and the
destructor `~X()`. The compiler defines each of these if it is used, unless the
default has been suppressed.

```cpp
struct Named_map {
public:
    // ... no default operations declared ...
private:
    string name;
    map<int, int> rep;
};

Named_map nm;        // default construct
Named_map nm2 {nm};  // copy construct
```

Since `std::map` and `std::string` have all the special functions, no further
work is needed. Copying, moving and destroying a `Named_map` are correct because
copying, moving and destroying its members are correct, and the compiler
composes them.

This is known as "the rule of zero".

## Decision

Build types out of members that manage themselves, and declare none of the six
default operations.

- Prefer a member of a type that already handles its own lifetime — a
  `std::string`, a container, a `std::unique_ptr`, a `std::fstream` — over a raw
  handle plus hand-written cleanup.
- Declare no constructor, destructor, copy or move operation unless the class
  needs behaviour the compiler cannot generate.
- If a `(pointer, size)` pair of members and a destructor that deletes the
  pointer would be needed, use a `std::vector` instead and delete all three.
- If any one of the six must be declared, declare or `=delete` the whole set,
  per
  [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md).

## Rationale

The compiler-generated operations are correct by construction and stay correct
when a member is added; a hand-written set is correct only as of the last time
someone checked, and adding a member silently makes it wrong. That asymmetry is
the entire argument, and it grows with the age of the code rather than
shrinking.

Not declaring the operations also means never getting their subtleties wrong —
the misspelling, the missing `const`, the `&` where `&&` was meant — because
there is nothing to spell. The composition of correct members is the cheapest
correctness available in the language.

## Consequences

### Positive

- Adding or removing a member keeps every operation correct without further
  work.
- No opportunity to misdeclare an operation, since none is declared.
- Resource release follows from
  [0014](0014-p-8-do-not-leak-any-resources.md) automatically, member by member.

### Negative

- It pushes the problem into the members: a type is only as safe as the handle
  types available, and if a resource has no RAII wrapper yet, one must be
  written, which is the very work this rule was meant to avoid.
- Compiler-generated operations are silent about intent. A reader cannot tell
  whether a type is copyable by design or copyable by omission, and a later
  member can remove copyability without any edit to the class.
- The generated copy is a member-wise copy, which is right far more often than
  it is right for free: a deep-copy or a shared-state type looks the same in the
  declaration and behaves entirely differently.

### Neutral

- The rule is about declaring the operations, not about which ones exist. A
  rule-of-zero type gets whichever of the six its members permit: a
  `std::unique_ptr` member leaves the copy operations defined as deleted without
  anything being written in the class.
- A destructor declared solely to make it `virtual` is a case for
  [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md),
  and takes the class out of the rule of zero.

## Enforcement

- Not enforceable directly. A good static analyser can nonetheless detect
  patterns that suggest an improvement: for example, a class holding a
  `(pointer, size)` pair of members with a destructor that deletes the pointer
  could probably be a `std::vector`.

## Related

- [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md)
  — what to do once this record's advice cannot be followed.
- [0054](0054-c-30-define-a-destructor-if-a-class-needs-an-explicit-action-at-object-destruction.md)
  — the narrow case where a destructor is genuinely required.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the guarantee that generated
  operations deliver for free.
- [0049](0049-c-11-make-concrete-types-regular.md) — regularity is usually
  obtained by declaring nothing at all.
