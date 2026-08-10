# 0102. C.127: A class with a virtual function should have a virtual or protected destructor

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A class with a virtual function is usually, and in general, used via a pointer
to base. Usually the last user has to call `delete` on a pointer to base, often
via a smart pointer to base, so the destructor should be public and virtual.
Less commonly, if deletion through a pointer to base is not intended to be
supported, the destructor should be protected and non-virtual.

```cpp
// bad
struct B {
    virtual int f() = 0;
    // ... no user-written destructor, defaults to public non-virtual ...
};

// bad: derived from a class without a virtual destructor
struct D : B {
    string s {"default"};
    // ...
};

void use()
{
    unique_ptr<B> p = make_unique<D>();
    // ...
} // undefined behavior, might call B::~B only and leak the string
```

There are people who do not follow this rule because they plan to use a class
only through a `shared_ptr`:

```cpp
std::shared_ptr<B> p = std::make_shared<D>(args);
```

Here the shared pointer will take care of deletion, because it captures the
deleter for the concrete type at construction, so no leak will occur from an
inappropriate `delete` of the base. People who do this consistently can get a
false positive from the rule. But the rule is important — what if one object was
allocated using `make_unique`? It is not safe unless the author of `B` ensures
that it can never be misused, such as by making all constructors private and
providing a factory function that enforces allocation with `make_shared`.

## Decision

Give every class with a virtual function an explicitly declared destructor, and
choose between the two forms deliberately.

- If objects may be deleted through a pointer or smart pointer to the base, make
  the destructor `public` and `virtual`, normally `virtual ~B() = default;`.
- If deletion through the base is not intended, make it `protected` and
  non-virtual, which makes the misuse a compile error rather than undefined
  behaviour.
- Do not rely on the destructor being implicitly declared; a class with a
  virtual function that has no user-declared destructor gets a public
  non-virtual one, which is the bad case.
- Do not rely on `shared_ptr`'s type-erased deleter as a substitute; it only
  saves the case where every object is in fact created through a `shared_ptr`,
  and nothing enforces that unless the constructors are private.

## Rationale

The strongest argument is that the correct choice is free and the incorrect one
is silent. Adding `virtual ~B() = default;` costs one line and no run-time
overhead beyond the vtable the class already has; omitting it costs undefined
behaviour that usually manifests as a leak, which no test failure points at.

The protected-destructor alternative deserves more attention than it usually
gets. It is not a lesser option: it converts "you must not delete through this
pointer" from a comment into a rule the compiler enforces, and it avoids the
vtable entry a virtual destructor adds.

## Consequences

### Positive

- Deleting through a base pointer destroys the whole object, so derived state is
  released.
- The protected form turns an unsupported operation into a compile error.
- Declaring the destructor explicitly makes the class's intended ownership model
  visible in its interface.

### Negative

- A virtual destructor makes the class non-trivially destructible, which
  disqualifies it from some optimisations and from being trivially copyable.
- Declaring any destructor suppresses the implicit move operations, so a base
  that declares `~B()` must also declare its move constructor and move
  assignment or lose them silently.
- Applying the rule mechanically produces false positives on classes used only
  through `shared_ptr`, where the vtable entry is genuinely wasted.

### Neutral

- The rule is about classes with virtual functions; a class with no virtual
  function does not want a virtual destructor.
- Whether the protected form is right depends on whether polymorphic ownership
  is part of the design, which is a decision this rule forces rather than makes.

## Enforcement

- A class with any virtual functions should have a destructor that is either
  public and virtual, or else protected and non-virtual.
- Flag `delete` of a class with a virtual function but no virtual destructor.

## Related

- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the same leak, shown from the interface side.
- [0101](0101-c-126-an-abstract-class-typically-doesnt-need-a-user-written-constructor.md)
  — the destructor is the one special member an abstract class does need.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the failure this rule
  prevents.
- [0105](0105-c-130-for-making-deep-copies-of-polymorphic-classes-prefer-a-virtual-clone-function-instead-of-public-copy-construction-assignment.md)
  — the other special members of a polymorphic base.
