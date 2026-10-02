# 0366. T.143: Don't write unintentionally non-generic code

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Generality and reusability are lost cheaply, by committing to details that were
never required. Use the most general facility that does the job.

Comparing iterators is the standard example. Use `!=` rather than `<`: it works
for more types, because it does not rely on an ordering.

```cpp
for (auto i = first; i < last; ++i) {   // less generic
    // ...
}

for (auto i = first; i != last; ++i) {   // good; more generic
    // ...
}
```

A range-`for` is better still where it does what you want.

The same applies to parameter types: use the least-derived class that has the
functionality you need.

```cpp
class Base {
public:
    Bar f();
    Bar g();
};

class Derived1 : public Base {
public:
    Bar h();
};

class Derived2 : public Base {
public:
    Bar j();
};

// bad, unless there is a specific reason for limiting to Derived1 objects only
void my_func(Derived1& param)
{
    use(param.f());
    use(param.g());
}

// good, uses only Base interface so only commit to that
void my_func(Base& param)
{
    use(param.f());
    use(param.g());
}
```

In each case the narrower version costs nothing to write and excludes callers
for no reason the code can point to.

## Decision

Commit only to what the code actually uses.

- Compare iterators with `!=`, not `<`, and prefer a range-`for` where one
  applies.
- Ask for emptiness with `empty()` rather than `size() == 0`, since emptiness is
  meaningful for containers that do not know their size.
- Declare a parameter as the least-derived type whose operations the body calls.
- Prefer the general standard-library facility to a container-specific one where
  both do the job.
- Where a narrower commitment is deliberate, make the reason visible, so that
  the restriction reads as a decision rather than an oversight.

## Rationale

The word in the title is "unintentionally": nobody chooses to exclude callers,
they just write the first thing that works for the type in front of them. Since
the general form is usually no longer and no slower, the narrow one is a cost
with no matching benefit — and the cost is only discovered later, by the person
who has a type that would have worked.

## Consequences

### Positive

- Code works for more types without being rewritten.
- Requirements on arguments shrink to the ones the body actually imposes.
- The generalisations named here are free at run time; `!=` and `empty()` are
  no slower than what they replace.

### Negative

- `!=` is less forgiving than `<`: a loop that overshoots its end runs away,
  where the ordered comparison would have stopped. Generality here trades a
  contained bug for an unbounded one.
- Taking the least-derived parameter type pulls against making interfaces
  precisely typed; a function that accepts `Base` accepts arguments the caller
  may have had no intention of allowing.
- Generality is speculative. Writing for types that never arrive spends effort
  and complicates the code that did have one job.
- A body that changes may quietly acquire a requirement its declared parameter
  type no longer justifies, and nothing detects that.

### Neutral

- The rule concerns unintentional narrowness; a deliberate restriction is
  legitimate and only needs stating.
- Concepts are the tool for saying exactly what a template requires, and the
  same reasoning applies to choosing them.

## Enforcement

- Flag comparison of iterators using `<` instead of `!=`.
- Flag `x.size() == 0` when `x.empty()` or `x.is_empty()` is available.
  Emptiness works for more containers than `size()`, because some do not know
  their size or are conceptually unbounded.
- Flag functions that take a pointer or reference to a more-derived type but
  use only functions declared in a base type.

## Related

- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — stating requirements precisely is how a generic interface avoids
  over-committing.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  opposing pull, which this rule has to be balanced against.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — what the least-derived type should look like when it is the parameter type.
- [0173](0173-es-2-prefer-suitable-abstractions-to-direct-use-of-language-features.md)
  — the same instinct applied to the choice of facility.
