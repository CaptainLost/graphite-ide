# 0343. T.62: Place non-dependent class template members in a non-templated base class

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Members of a class template that do not mention any template parameter can be
used without specifying template arguments and without instantiating the
template at all — but only if they are not members of the template. Putting
them in a non-templated base class buys exactly that.

```cpp
template<typename T>
class Foo {
public:
    enum { v1, v2 };
    // ...
};
```

`Foo<int>::v1` and `Foo<string>::v1` are the same value declared twice, and
there is no way to name the enumerator without picking a `T` that is irrelevant
to it. Lifting it into a base removes the choice:

```cpp
struct Foo_base {
    enum { v1, v2 };
    // ...
};

template<typename T>
class Foo : public Foo_base {
public:
    // ...
};
```

A more general version of this rule would be: if a class template member
depends on only N of M template parameters, place it in a base class with only
those N parameters. For N equal to one, there is a choice between such a base
class and a class in the surrounding scope, which is the form the
over-parameterization rule uses.

## Decision

Do not leave a member inside a class template when it does not depend on the
template's parameters.

- Collect the parameter-independent enumerators, constants, type aliases and
  member functions into a non-templated base class, and derive the template
  from it.
- Where a member depends on some but not all of the parameters, put it in a
  base class parameterised on just those.
- Name the base for its role, not for the template it serves, since callers
  will now use it directly.
- Keep the base's members accessible on the derived template so existing
  spellings such as `Foo<int>::v1` continue to work.

## Rationale

The instantiation cost is the smaller half of the argument. The larger half is
that a caller who only needs the enumerator is currently forced to name a type
argument, and that argument becomes part of their code for no reason — it will
be wrong the day the template gains or loses a parameter. Moving the member to
a base makes the dependency honest, and honesty here is checkable by the
compiler: if the base compiles, the member really was independent.

## Consequences

### Positive

- Parameter-independent members can be used without instantiating the template.
- One definition exists rather than one per instantiation, so constants compare
  equal and types are interchangeable across instantiations.
- The class template's remaining members are, by construction, the ones that
  actually depend on its parameters.

### Negative

- It buys inheritance where there was none. A base class is part of the type:
  it affects layout, empty-base optimisation, name lookup, and what a derived
  class may do with the name.
- The base is public API whether or not that was wanted, so it needs a name, a
  header, and a documented meaning.
- Upstream leaves the treatment of constants and class statics explicitly
  open, and offers no enforcement beyond a placeholder, so the rule's edges are
  undrawn.
- Applied enthusiastically, it produces a chain of small bases whose only
  purpose is to record which parameters each member uses, which reads worse
  than the single class it replaced.

### Neutral

- The base need not be abstract or polymorphic; it is a place to put
  declarations, not an interface.
- Whether to use a base class or a namespace-scope entity is a style choice
  when there is one remaining parameter.

## Related

- [0342](0342-t-61-do-not-over-parameterize-members-scary.md) — the general
  rule, of which this is the N-equals-zero case.
- [0353](0353-t-84-use-a-non-template-core-implementation-to-provide-an-abi-stable-interface.md)
  — the same technique used for stability rather than for usability.
- [0349](0349-t-80-do-not-naively-templatize-a-class-hierarchy.md) — a stable
  non-templated base is also the answer to a templatized hierarchy.
