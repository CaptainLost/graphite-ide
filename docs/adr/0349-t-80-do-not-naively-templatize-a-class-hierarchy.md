# 0349. T.80: Do not naively templatize a class hierarchy

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Templating a class hierarchy that has many functions, especially many virtual
functions, can lead to code bloat.

```cpp
template<typename T>
struct Container {         // an interface
    virtual T* get(int i);
    virtual T* first();
    virtual T* next();
    virtual void sort();
};

template<typename T>
class Vector : public Container<T> {
public:
    // ...
};

Vector<int> vi;
Vector<string> vs;
```

Defining `sort` as a member function of a container is probably a bad idea to
begin with, but it is not unheard of and it makes a good example of what not to
do. Because the functions are virtual, the compiler cannot know whether
`Vector<int>::sort()` is ever called, so it must generate code for it — and
again for `Vector<string>::sort()`. Unless both are called, that is pure code
bloat. Now imagine a hierarchy with dozens of member functions and dozens of
derived classes, each instantiated for several types.

In many cases a stable interface can be provided by not parameterising the
base: the polymorphic part of the design is expressed once, in a non-templated
class, and the template adds type safety on top of it.

## Decision

Do not add a template parameter to a whole class hierarchy without asking what
each member does with it.

- Keep the polymorphic core — the virtual functions and the state they operate
  on — in a non-templated base class.
- Add the template only as a thin, mostly inline, type-safe wrapper over that
  core.
- Do not declare a virtual function whose signature depends on a template
  parameter unless the dynamic dispatch genuinely has to be per-instantiation.
- Move members that do not use the parameter out of the template entirely.
- Where the parameter varies only the element type of a container, consider
  whether run-time polymorphism is needed at all.

## Rationale

Virtual functions defeat the one mechanism that normally limits template code
size. An ordinary member function of a class template is only instantiated if
it is used, but a virtual function must be emitted because its address goes in
the vtable, so the cost is paid per instantiation whether or not anyone calls
it. Multiplying two mechanisms that each generate code — the hierarchy and the
template — gives a product, not a sum.

## Consequences

### Positive

- Binary size stays proportional to the polymorphic design rather than to the
  number of instantiations.
- The stable, non-templated base gives clients a type they can name without
  choosing a template argument.
- Link times and compile times improve, since the shared code exists once.

### Negative

- Splitting a hierarchy into a non-templated core and a templated wrapper means
  casting between them, and those casts are the kind of unchecked step the
  guidelines otherwise discourage.
- The core has to be written in terms of erased or untyped state, which loses
  the type safety inside the implementation even though the wrapper restores it
  outside.
- The rule tells you not to do the naive thing without giving a threshold; a
  small hierarchy with two virtual functions and two instantiations is fine, and
  the record cannot say where fine stops.
- Upstream itself marks the enforcement as prone to false positives, so a tool
  applying it will complain about designs that are correct.

### Neutral

- Templating a hierarchy is not forbidden, only doing it naively.
- The alternative of dropping the hierarchy and keeping the template is often
  the right answer for containers, but that is a design decision beyond this
  rule.

## Enforcement

- Flag virtual functions that depend on a template argument. Expect false
  positives; some such functions are the point of the design.

## Related

- [0353](0353-t-84-use-a-non-template-core-implementation-to-provide-an-abi-stable-interface.md)
  — the technique this rule points at, worked through.
- [0342](0342-t-61-do-not-over-parameterize-members-scary.md) — the same
  question asked of individual members.
- [0098](0098-c-120-use-class-hierarchies-to-represent-concepts-with-inherent-hierarchical-structure-only.md)
  — whether the hierarchy should exist at all.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — code bloat is wasted space,
  and it is paid by everyone who links the library.
