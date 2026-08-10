# 0342. T.61: Do not over-parameterize members (SCARY)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A member that does not depend on a template parameter cannot be used except for
a specific template argument. This limits use and typically increases code
size.

```cpp
template<typename T, typename A = std::allocator<T>>
    // requires Regular<T> && Allocator<A>
class List {
public:
    struct Link {   // does not depend on A
        T elem;
        Link* pre;
        Link* suc;
    };
    using iterator = Link*;
    iterator first() const { return head; }
    // ...
private:
    Link* head;
};

List<int> lst1;
List<int, My_allocator> lst2;
```

This looks innocent enough, but `Link` now formally depends on the allocator
even though it does not use it. `List<int>::Link` and
`List<int, My_allocator>::Link` are unrelated types, which forces redundant
instantiations that can be surprisingly costly in real-world scenarios.
Typically the solution is to make what would have been a nested class
non-local, with its own minimal set of template parameters:

```cpp
template<typename T>
struct Link {
    T elem;
    Link* pre;
    Link* suc;
};

template<typename T, typename A = std::allocator<T>>
    // requires Regular<T> && Allocator<A>
class List2 {
public:
    using iterator = Link<T>*;
    iterator first() const { return head; }
    // ...
private:
    Link<T>* head;
};

List2<int> lst1;
List2<int, My_allocator> lst2;
```

Some people found the idea that the `Link` was no longer hidden inside the list
scary, so the technique was named SCARY. From the academic paper the name comes
from: "The acronym SCARY describes assignments and initializations that are
Seemingly erroneous (appearing Constrained by conflicting generic parameters),
but Actually work with the Right implementation (unconstrained bY the conflict
due to minimized dependencies)."

The same applies to lambdas that do not depend on all of the enclosing
template's parameters.

## Decision

Give every member of a class template only the template parameters it actually
uses.

- Move a nested type that does not use every parameter out of the class
  template and give it its own parameter list.
- Do the same for member functions, member type aliases, variable templates and
  lambdas that do not depend on every parameter.
- Refer to the extracted entity through an alias inside the class template
  (`using iterator = Link<T>*;`) so that the public spelling does not change.
- Accept that the extracted type is now visible outside the class; that
  visibility is the point, not an accident.

## Rationale

Two instantiations that differ only in a parameter neither of them consults
should be the same type, and when they are not, the difference is invisible to
the reader and paid for by the compiler. Beyond the code size, the real damage
is to the interface: an iterator that formally depends on the allocator cannot
be passed between two lists that hold the same elements, and no amount of
casting makes that legitimate.

## Consequences

### Positive

- Iterators and nodes become interchangeable across instantiations that differ
  only in an unused parameter.
- Redundant instantiations disappear, cutting object size and compile time.
- The dependency structure is written down: reading the parameter list of a
  member tells you what it can possibly depend on.

### Negative

- Encapsulation is genuinely lost. A helper that was private to the class
  template becomes a namespace-scope name that must be documented, named
  carefully, and kept out of the way — usually in a `detail` namespace, which
  reintroduces some of the noise it removed.
- The transformation is mechanical but not free: existing code naming
  `List<T, A>::Link` has to change, and an alias only covers the spellings you
  thought to provide.
- The rule pushes against grouping related declarations together, so a class
  template's implementation ends up spread over several namespace-scope
  entities.
- Not every over-parameterized member can be extracted; one that uses a private
  member of the enclosing template needs a friendship or an accessor to move
  out.

### Neutral

- The name SCARY describes the resulting assignments, not the technique's
  difficulty.
- Which parameters a member "uses" is a syntactic question, so this is one of
  the few template rules a tool can answer exactly.

## Enforcement

- Flag member types that do not depend on every template parameter.
- Flag member functions that do not depend on every template parameter.
- Flag lambdas or variable templates that do not depend on every template
  parameter.

## Related

- [0343](0343-t-62-place-non-dependent-class-template-members-in-a-non-templated-base-class.md)
  — the limiting case, where a member depends on no parameter at all.
- [0341](0341-t-60-minimize-a-templates-context-dependencies.md) — the same
  minimisation applied to the definition's context rather than its parameters.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — redundant instantiations are
  wasted space, and wasted compile time.
