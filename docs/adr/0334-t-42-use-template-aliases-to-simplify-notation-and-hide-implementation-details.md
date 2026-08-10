# 0334. T.42: Use template aliases to simplify notation and hide implementation details

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Improved readability and implementation hiding. Template aliases replace many
uses of traits to compute a type, and they can also be used to wrap a trait.

```cpp
template<typename T, size_t N>
class Matrix {
    // ...
    using Iterator = typename std::vector<T>::iterator;
    // ...
};
```

This saves the user of `Matrix` from having to know that its elements are stored
in a `vector`, and from repeatedly typing `typename std::vector<T>::`.

```cpp
template<typename T>
void user(T& c)
{
    // ...
    typename container_traits<T>::value_type x; // bad, verbose
    // ...
}

template<typename T>
using Value_type = typename container_traits<T>::value_type;
```

This saves the user of `Value_type` from having to know the technique used to
implement value types.

```cpp
template<typename T>
void user2(T& c)
{
    // ...
    Value_type<T> x;
    // ...
}
```

A simple, common use could be summarised as: wrap traits.

## Decision

Give a computed type a name, and let callers use the name.

- Publish a member alias for each type a class template's users need —
  `value_type`, `iterator`, `size_type` — instead of exposing the type it is
  implemented in terms of.
- Wrap every trait access that appears more than once in an alias template, so
  `typename some_traits<T>::value_type` is written once and used as
  `Value_type<T>`.
- Prefer the standard library's `_t` aliases to their `::type` members, for the
  same reason.
- Change the underlying type behind the alias when the implementation changes;
  that freedom is the point of publishing the alias rather than the type.

## Rationale

The `typename ...::type` spelling is not merely verbose: it forces every caller
to name the implementation vehicle, so the vehicle becomes part of the interface
by repetition and cannot be replaced afterwards. An alias converts a
substitution failure that used to occur in the caller's declaration into one that
occurs in the alias, and gives the reader a domain word — `Value_type<T>` — in
the place where an implementation detail used to be.

## Consequences

### Positive

- Callers stop naming the container, the trait class, and the `typename`
  disambiguator.
- The implementation type can be changed without touching users.
- Declarations read in the vocabulary of the abstraction rather than of its
  plumbing.
- Standard-library conventions are followed, so generic code that expects
  `value_type` works.

### Negative

- An alias adds a level of indirection to every diagnostic: the error message
  still names the underlying type, so the reader must resolve the alias by hand
  to connect the two.
- An alias that expands to a nested dependent type is not deducible: a parameter
  of type `Value_type<T>` leaves `T` in a non-deduced context, so `T` has to be
  deduced from another parameter or written out at the call.
- Alias templates cannot be specialised or partially specialised, so an alias
  that later needs to vary by case must become a class template again, changing
  its users.
- Publishing an alias is a promise; retracting it later is a breaking change,
  so aliases accumulate.

### Neutral

- The rule is about naming computed types, not about type aliases generally;
  which keyword to use is
  [0335](0335-t-43-prefer-using-over-typedef-for-defining-aliases.md).
- Wrapping a trait does not remove the trait; it removes the need for users to
  know it.

## Enforcement

- Flag the use of `typename` as a disambiguator outside `using` declarations.

## Related

- [0335](0335-t-43-prefer-using-over-typedef-for-defining-aliases.md) — the
  syntax an alias is written in, and the only one that can be a template.
- [0092](0092-c-100-follow-the-stl-when-defining-a-container.md) — the member
  alias names a container is expected to publish.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — an alias is encapsulation applied to a type computation.
- [0042](0042-c-3-represent-the-distinction-between-an-interface-and-an-implementation-using-a-class.md)
  — the interface/implementation split this maintains for types.
