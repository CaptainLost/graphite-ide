# 0149. R.3: A raw pointer (a T*) is non-owning

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Nothing in the C++ standard, and nothing in most code, says that a `T*` owns
what it points to, and most raw pointers do not. Owning pointers need to be
identifiable so that the objects they point to can be deleted reliably and
efficiently.

```cpp
void f()
{
    int* p1 = new int{7};           // bad: raw owning pointer
    auto p2 = make_unique<int>(7);  // OK: the int is owned by a unique pointer
    // ...
}
```

The `unique_ptr` protects against leaks by guaranteeing the deletion of its
object, even in the presence of exceptions. The `T*` does not.

The same ambiguity infects members:

```cpp
template<typename T>
class X {
public:
    T* p;   // bad: it is unclear whether p is owning or not
    T* q;   // bad: it is unclear whether q is owning or not
    // ...
};
```

Making ownership explicit fixes it:

```cpp
template<typename T>
class X2 {
public:
    owner<T*> p;  // OK: p is owning
    T* q;         // OK: q is not owning
    // ...
};
```

`owner<T*>` has no semantics beyond `T*`. It can be introduced without changing
any code that uses it and without affecting an ABI; it is an indicator to
programmers and analysis tools. If an `owner<T*>` is a member of a class, that
class had better have a destructor that deletes it.

Returning a raw pointer imposes a lifetime management uncertainty on the caller
— who deletes the pointed-to object?

```cpp
Gadget* make_gadget(int n)
{
    auto p = new Gadget{n};
    // ...
    return p;
}
void caller(int n)
{
    auto p = make_gadget(n);   // remember to delete p
    // ...
    delete p;
}
```

Besides the leak this invites, it adds a spurious allocation and deallocation
and is needlessly verbose. If `Gadget` is cheap to move out of a function —
small, or with an efficient move operation — return it by value:

```cpp
Gadget make_gadget(int n)
{
    Gadget g{n};
    // ...
    return g;
}
```

This applies to factory functions. Where pointer semantics really are required,
because the return type must refer to a base class of a hierarchy, return a
smart pointer rather than a raw one.

The major exception is legacy code, especially code that must remain compilable
as C or interface with C and C-style C++ through an ABI. Billions of lines
violate this rule, and they cannot all be rewritten. The problem also cannot be
solved at scale by turning every owning pointer into a `unique_ptr` or
`shared_ptr`, partly because owning raw pointers are needed in the
implementation of the fundamental resource handles themselves — a typical
`vector` implementation holds one owning pointer and two non-owning ones. Some
interfaces cannot even be annotated with `owner`, because they must remain
compilable as C.

## Decision

Treat every raw pointer as non-owning, and never make one the sole owner of a
resource.

- Do not assign the result of `new`, or of any allocating function, to a raw
  pointer variable or member.
- Give ownership to a resource handle: `unique_ptr` for exclusive ownership,
  `shared_ptr` for shared ownership, a container for a sequence.
- Return results by value. Return a smart pointer only when reference semantics
  are genuinely required, such as a polymorphic result from a factory.
- Where an owning raw pointer is unavoidable — inside a resource handle's own
  implementation, or across a C ABI — mark it `owner<T*>` so that the ownership
  is stated and tool-visible, and make sure exactly one code path releases it.
- A raw pointer member of a class means "this class does not free this"; a
  destructor that deletes a plain `T*` member is a defect in the declaration,
  not in the destructor.

## Rationale

This rule buys its value from being universal rather than from being right in
any one case. A codebase in which `T*` sometimes owns has no way to answer "who
deletes this?" except by reading everything; a codebase in which `T*` never owns
answers it from the declaration. `owner<T*>` exists precisely because the
guarantee has to survive the places where a smart pointer cannot go: it changes
nothing about the generated code and everything about what a reader and a
checker can conclude.

## Consequences

### Positive

- "Who deletes this?" is answerable from a declaration.
- Leaks and double deletes of the classic shape — two people with different
  assumptions about one signature — stop being possible by construction.
- Returning by value removes an allocation and a deallocation as well as the
  ambiguity.

### Negative

- The rule has a permanent exception at C ABI boundaries and inside resource
  handles, which is where the mistakes are hardest to find.
- `owner<T*>` is documentation with tool support, not a guarantee: nothing stops
  an `owner<T*>` from being dropped on some path, and nothing makes the compiler
  care.
- The annotation lives in a support library, so using it means taking on that
  dependency, and a header that must also compile as C cannot carry it at all —
  which leaves exactly the C-interoperating cases unannotated.
- Returning by value is not always available: a polymorphic result forces a
  smart pointer, which is the less elegant option the guidelines themselves
  concede.

### Neutral

- The rule says a raw pointer does not own; it says nothing about how long the
  pointee lives, which remains the caller's promise.
- `owner<T*>` changes no generated code and no ABI, so it can be introduced a
  declaration at a time; the rule itself holds whether or not the annotation is
  used.

## Enforcement

- (Simple) Warn on `delete` of a raw pointer that is not an `owner<T>`.
- (Moderate) Warn on failure to either reset or explicitly delete an `owner<T>`
  pointer on every code path.
- (Simple) Warn if the return value of `new` is assigned to a raw pointer.
- (Simple) Warn if a function returns an object that was allocated within the
  function but has a move constructor. Suggest returning it by value instead.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the interface half of the same rule.
- [0150](0150-r-4-a-raw-reference-a-t-is-non-owning.md) — the same statement for
  references.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) —
  what owns instead.
- [0014](0014-p-8-do-not-leak-any-resources.md) — why identifying owners matters
  at all.
