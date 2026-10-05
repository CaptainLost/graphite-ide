# 0320. T.5: Combine generic and OO techniques to amplify their strengths, not their costs

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Generic and object-oriented techniques are complementary, and the rule is about
the two directions in which they help each other.

Static helps dynamic: use static polymorphism to implement dynamically
polymorphic interfaces.

```cpp
class Command {
    // pure virtual functions
};

// implementations
template</*...*/>
class ConcreteCommand : public Command {
    // implement virtuals
};
```

Dynamic helps static: offer a generic, comfortable, statically bound interface,
but dispatch dynamically inside, so that a uniform object layout is presented.
Type erasure, as used for `std::shared_ptr`'s deleter, is the standard example —
though it should not be overused.

```cpp
#include <memory>

class Object {
public:
    template<typename T>
    Object(T&& obj)
        : concept_(std::make_shared<ConcreteCommand<T>>(std::forward<T>(obj))) {}

    int get_id() const { return concept_->get_id(); }

private:
    struct Command {
        virtual ~Command() {}
        virtual int get_id() const = 0;
    };

    template<typename T>
    struct ConcreteCommand final : Command {
        ConcreteCommand(T&& obj) noexcept : object_(std::forward<T>(obj)) {}
        int get_id() const final { return object_.get_id(); }
    private:
        T object_;
    };

    std::shared_ptr<Command> concept_;
};

class Bar {
public:
    int get_id() const { return 1; }
};

struct Foo {
public:
    int get_id() const { return 2; }
};

Object o(Bar{});
Object o2(Foo{});
```

There is a cost to watch. In a class template, non-virtual member functions are
instantiated only if they are used, but virtual functions are instantiated every
time. This can bloat code size, and it can overconstrain a generic type by
instantiating functionality that is never needed. Avoid this, even though the
standard-library facets made that mistake.

## Decision

Use each technique where it is strong, and do not let one of them drag in the
other's costs.

- Implement a dynamically polymorphic interface with a template that derives from
  it, rather than writing one hand-copied implementation class per concrete type.
- Where a uniform layout or a single non-templated type is needed at the
  boundary, put the dynamic dispatch inside and keep the outside statically
  typed and deduced.
- Do not declare a virtual member of a class template unless every instantiation
  can support it: virtuals instantiate unconditionally and so become requirements
  on every argument type.
- Reach for the erasing wrapper only when the uniform type is actually needed;
  the default is not to erase, per
  [0340](0340-t-49-where-possible-avoid-type-erasure.md).

## Rationale

The two techniques fail in opposite places — templates cannot give a single
runtime type, hierarchies cannot give per-type code — so each one's weakness is
the other's normal case. The concrete trap worth remembering from this rule is
narrow and mechanical: a virtual function in a class template is instantiated
whether or not anyone calls it, which silently converts an optional capability
into a requirement on every argument type, and the standard library itself got
this wrong.

## Consequences

### Positive

- One template replaces a family of near-identical concrete implementations of an
  abstract interface.
- Callers get deduction and value semantics while the boundary keeps one type.
- The virtual-instantiation trap is stated explicitly, which is the part most
  easily missed.

### Negative

- The erasing wrapper shown here allocates and adds an indirection per call, and
  hides both from the caller.
- Mixed designs are harder to read than either pure style: the reader must hold
  both a hierarchy and a template in mind, and errors can surface in either.
- "Amplify strengths, not costs" is not a test anyone can apply mechanically, so
  the rule mostly settles arguments after the fact.
- Putting a template constructor on a type-erasing class makes it greedy in
  overload resolution, which is the hazard
  [0338](0338-t-47-avoid-highly-visible-unconstrained-templates-with-common-names.md)
  describes.

### Neutral

- The upstream See-also list is a set of unresolved cross-reference stubs and is
  not reproduced.
- The example's `Object` names its erased interface `Command`, shadowing the
  earlier standalone `Command` class; the two examples are independent.

## Enforcement

- No check of its own. The rule defers to the more specific template rules,
  chiefly those on type erasure and on not naively templatising a hierarchy.

## Related

- [0340](0340-t-49-where-possible-avoid-type-erasure.md) — the counterweight to
  the "dynamic helps static" half.
- [0048](0048-c-10-prefer-concrete-types-over-class-hierarchies.md) — prefer a
  concrete type before reaching for either technique.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the shape of the abstract interface a template implements.
- [0248](0248-per-13-eliminate-redundant-indirections.md) — the cost the dynamic
  half adds.
- [0563](0563-write-concrete-code-and-keep-templates-for-vocabulary-types.md) — amends this record: concrete code first.
