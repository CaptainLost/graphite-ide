# 0057. C.33: If a class has an owning pointer member, define a destructor

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

An owned object must be deleted upon destruction of the object that owns it.

A pointer member could represent a resource. A `T*` should not do so, but in
older code that is common, so consider a `T*` a possible owner and therefore
suspect.

```cpp
template<typename T>
class Smart_ptr {
    T* p;   // BAD: vague about ownership of *p
    // ...
public:
    // ... no user-defined default operations ...
};

void use(Smart_ptr<int> p1)
{
    // error: p2.p leaked (if not nullptr and not owned by some other code)
    auto p2 = p1;
}
```

Note that once a destructor is defined, all the default operations must be
defined or deleted:

```cpp
template<typename T>
class Smart_ptr2 {
    T* p;   // BAD: vague about ownership of *p
    // ...
public:
    // ... no user-defined copy operations ...
    ~Smart_ptr2() { delete p; }  // p is an owner!
};

void use(Smart_ptr2<int> p1)
{
    auto p2 = p1;   // error: double deletion
}
```

The default copy operation simply copies `p1.p` into `p2.p`, leading to a double
destruction of `p1.p`. Be explicit about ownership:

```cpp
template<typename T>
class Smart_ptr3 {
    owner<T*> p;   // OK: explicit about ownership of *p
    // ...
public:
    // ...
    // ... copy and move operations ...
    ~Smart_ptr3() { delete p; }
};

void use(Smart_ptr3<int> p1)
{
    auto p2 = p1;   // OK: no double deletion
}
```

Often the simplest way to get a destructor is to replace the pointer with a
smart pointer, such as `std::unique_ptr`, and let the compiler arrange for
proper destruction implicitly. The guideline does not require all owning
pointers to be smart pointers, because that would sometimes demand non-trivial
code changes and might affect ABIs.

## Decision

If a member owns what it points to, make sure destruction releases it, and make
sure copying does not double-release it.

- Prefer to replace the owning raw pointer with a `std::unique_ptr` or
  `std::shared_ptr`, which supplies the destructor and the correct copy or move
  semantics for free.
- Where a raw pointer must be kept, mark it `gsl::owner<T*>` and write the
  destructor that deletes it.
- Having written that destructor, define or `=delete` the copy constructor, copy
  assignment, move constructor and move assignment as well, per
  [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md).
  A destructor with generated copy operations is a double free waiting to
  happen.
- Treat any raw pointer member in existing code as a suspected owner until its
  constructors have been read.

## Rationale

The instructive part of this rule is the progression through the three
`Smart_ptr` classes: no destructor leaks, a destructor with default copies
double-frees, and only the explicit form is correct. Neither of the first two
produces a diagnostic; both compile and both are wrong, in opposite directions.

That is why the answer is to delete the pointer rather than to write the
destructor. A `std::unique_ptr` member makes the leaking version impossible and
makes the double-free version a compile error, because the class stops being
copyable, which is exactly the correct default for something that owns a unique
resource.

## Consequences

### Positive

- Owned memory is released once, on every path, without hand-written code.
- The class's copyability follows automatically from what it owns.
- The double-free that generated copy operations produce becomes impossible
  rather than merely unlikely.

### Negative

- Replacing a raw pointer member with a smart pointer changes the class's size
  and layout in principle, and its copyability in practice, which can break
  callers that relied on the class being copyable — usually correctly, since
  those copies were the bug, but the breakage is still work.
- Where a raw owning pointer must be kept for ABI reasons, the rule delivers
  only the hand-written destructor plus four more declarations, and all the
  original opportunities for error remain.
- Marking a pointer `gsl::owner<T*>` is only documentation unless a checker that
  understands it is actually run.

### Neutral

- The rule concerns owning members; observers are unaffected and must not be
  deleted.
- `shared_ptr` satisfies the rule while keeping the class copyable, at the cost
  of shared rather than exclusive ownership.

## Enforcement

- A class with a pointer data member is suspect.
- A class with an `owner<T>` member should define its default operations.

## Related

- [0056](0056-c-32-if-a-class-has-a-raw-pointer-t-or-reference-t-consider-whether-it-might-be-owning.md)
  — the question this record assumes has been answered.
- [0052](0052-c-21-if-you-define-or-delete-any-copy-move-or-destructor-function-define-or-delete-them-all.md)
  — the obligation that follows from defining the destructor.
- [0054](0054-c-30-define-a-destructor-if-a-class-needs-an-explicit-action-at-object-destruction.md)
  — the general form of this rule.
- [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) — using a
  smart pointer returns the class to the rule of zero.
