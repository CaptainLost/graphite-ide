# 0092. C.100: Follow the STL when defining a container

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A container is an object holding a sequence of objects of some type;
`std::vector` is the archetypal container. The STL containers are familiar to
most C++ programmers and are a fundamentally sound design.

There are of course other fundamentally sound design styles, and sometimes
reasons to depart from the style of the standard library, but in the absence of
a solid reason to differ it is simpler and easier for both implementers and
users to follow the standard. In particular, `std::vector` and `std::map`
provide useful and relatively simple models.

```cpp
// simplified (e.g., no allocators):
template<typename T>
class Sorted_vector {
    using value_type = T;
    // ... iterator types ...
    Sorted_vector() = default;
    Sorted_vector(initializer_list<T>);    // initializer-list constructor: sort and store
    Sorted_vector(const Sorted_vector&) = default;
    Sorted_vector(Sorted_vector&&) = default;
    Sorted_vector& operator=(const Sorted_vector&) = default;   // copy assignment
    Sorted_vector& operator=(Sorted_vector&&) = default;        // move assignment
    ~Sorted_vector() = default;

    Sorted_vector(const std::vector<T>& v);   // store and sort
    Sorted_vector(std::vector<T>&& v);        // sort and "steal representation"

    const T& operator[](int i) const { return rep[i]; }
    // no non-const direct access to preserve order
    void push_back(const T&);   // insert in the right place (not necessarily at back)
    void push_back(T&&);        // insert in the right place (not necessarily at back)
    // ... cbegin(), cend() ...
private:
    std::vector<T> rep;  // use a std::vector to hold elements
};

template<typename T> bool operator==(const Sorted_vector<T>&, const Sorted_vector<T>&);
template<typename T> bool operator!=(const Sorted_vector<T>&, const Sorted_vector<T>&);
```

Here the STL style is followed, but incompletely, and that is not uncommon.
Provide only as much functionality as makes sense for the specific container.
The key is to define the conventional constructors, assignments, destructors and
iterators, as meaningful for that container, with their conventional semantics.
From that base the container can be expanded as needed; here, special
constructors from `std::vector` were added. Note also that `Sorted_vector`
withholds non-const element access, because handing out a mutable reference
would let a caller break the ordering the type exists to maintain.

## Decision

When defining a container, model it on the standard containers.

- Provide the conventional member type aliases — `value_type`, `size_type`,
  `iterator`, `const_iterator` — for whichever of them the container
  meaningfully has.
- Provide `begin()`, `end()`, `cbegin()` and `cend()`, so that the container
  works with range-`for` and the standard algorithms.
- Provide the conventional constructors, assignment operators and destructor
  with their conventional meanings, and name insertion and lookup operations as
  the standard library names them.
- Do not provide an operation whose name matches a standard one but whose
  semantics differ; if the meaning has to differ, choose a different name.
- Omit an operation the container cannot support efficiently or safely, rather
  than supplying a slow or invariant-breaking version of it.

## Rationale

Familiarity is the whole argument, and it is a stronger one than it sounds:
every departure from the standard shape is a fact a reader must learn and hold,
and a container is precisely the kind of type that appears in code written by
people who did not write it.

The subtler benefit is compatibility. A type with `begin()`, `end()` and
`value_type` works with the entire `<algorithm>` header, with range-`for`, and
with every generic function anyone writes afterwards, for free. A type that
spells them differently is cut off from all of it, and each connection has to be
rebuilt by hand.

## Consequences

### Positive

- Callers already know the interface before reading it.
- The container composes with standard algorithms and range-based `for` without
  adapters.
- Conventional special members make the type usable in standard containers and
  in generic code that copies or moves.

### Negative

- The STL interface is large, and matching it in full is a great deal of code —
  iterator types, `const` overloads, allocator plumbing — most of which a
  purpose-built container does not need.
- Standard names carry standard complexity expectations. `push_back` on
  `Sorted_vector` is not amortised constant time, so following the naming
  convention here quietly breaks the performance convention.
- Where the container's semantics genuinely differ from any standard one, the
  rule offers no guidance beyond "do not mislead", and the choice of what to
  omit is a judgement.

### Neutral

- Partial conformance is explicitly acceptable; the rule asks for the
  conventional subset, not the whole of `std::vector`.
- Nothing here prevents building the container on top of a standard one, which
  is usually the right implementation.

## Related

- [0093](0093-c-101-give-a-container-value-semantics.md),
  [0094](0094-c-102-give-a-container-move-operations.md),
  [0095](0095-c-103-give-a-container-an-initializer-list-constructor.md) and
  [0096](0096-c-104-give-a-container-a-default-constructor-that-sets-it-to-empty.md)
  — the specific obligations this rule generalises.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — prefer using a
  standard container to writing one.
- [0009](0009-p-3-express-intent.md) — a conventional name states intent that a
  novel one does not.
