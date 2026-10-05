# 0181. ES.11: Use auto to avoid redundant repetition of type names

Status: superseded by 0560
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Three reasons are given. Simple repetition is tedious and error-prone. When
`auto` is used, the name of the declared entity sits in a fixed position in the
declaration, which increases readability. And in a function template
declaration, the return type can be a member type that is awkward or impossible
to name at that point.

```cpp
auto p = v.begin();      // vector<DataRecord>::iterator
auto z1 = v[3];          // makes copy of DataRecord
auto& z2 = v[3];         // avoids copy
const auto& z3 = v[3];   // const and avoids copy
auto h = t.future();
auto q = make_unique<int[]>(s);
auto f = [](int x) { return x + 10; };
```

In each case, writing out a longish, hard-to-remember type that the compiler
already knows is avoided — and it is a type the programmer could get wrong. The
lambda case is not optional: the type of `f` has no spelling at all.

The trailing-return-type case:

```cpp
template<class T>
auto Container<T>::first() -> Iterator;   // Container<T>::Iterator
```

The exception is where the type matters more than the initialiser's type does:
avoid `auto` for initializer lists, and in cases where you know exactly which
type you want and where an initialiser might require conversion.

```cpp
auto lst = { 1, 2, 3 };   // lst is an initializer list
auto x{1};   // x is an int (in C++17; initializer_list in C++11)
```

As of C++20, concepts can and should be used to be more specific about the type
being deduced, which recovers the documentation that a bare `auto` gives up:

```cpp
forward_iterator auto p = algo(x, y, z);
```

`auto` also pairs with structured bindings to break out the members of a pair
without naming its type:

```cpp
std::set<int> values;
// ...
auto [ position, newly_inserted ] = values.insert(5);   // break out the members of the std::pair
```

## Decision

Use `auto` where the type is already stated by the initialiser, and spell the
type out where it is not.

- Declare iterators, results of factory functions, lambdas and other
  compiler-known types with `auto`.
- Write `auto&`, `const auto&` or `auto&&` deliberately; plain `auto` copies,
  and that is a decision, not a default.
- Use `auto` with structured bindings to decompose a pair or tuple rather than
  naming the pair type.
- Constrain a deduced type with a concept — `forward_iterator auto p = ...` —
  where the requirement on the type is what the reader needs to know; C++20
  makes that spelling available.
- Do not use `auto` where a conversion is intended, where the exact type matters
  and differs from the initialiser's, or with a braced initialiser list.

## Rationale

The type in `vector<DataRecord>::iterator it = v.begin();` is not documentation:
it is a restatement of something the right-hand side already determines, which
means it is a second place the truth can be written down wrongly, and a place
that must be edited when `v` changes type. `auto` deletes that second copy. The
secondary benefit — that the declared name always appears immediately after the
keyword — is what makes a column of `auto` declarations scannable in a way that
a column of differing type names is not.

## Consequences

### Positive

- Changing a container's type does not require editing every declaration that
  mentions its iterator.
- Types that cannot be named — lambdas, and many template-dependent types — get
  declared naturally rather than through a `decltype` incantation.
- The declared name is always in the same place, so declarations line up
  visually.

### Negative

- The reader loses the type from the source and must get it from an IDE, a
  compiler error, or the initialiser's definition. In a diff or a code review
  tool, none of those are available.
- Plain `auto` silently copies. `auto x = v[3];` on an expensive element is a
  performance bug that looks like nothing at all, and the reference-ness of a
  proxy type such as `vector<bool>`'s makes it worse.
- Deduction rules are subtle: `auto x{1}` and `auto x = {1}` declare different
  types, and this changed between C++11 and C++17.
- Overuse turns a file into a sequence of declarations that state nothing about
  what is in them, which trades one kind of redundancy for a loss of local
  reasoning.

### Neutral

- Constrained `auto` sits between the two extremes: it documents the requirement
  without naming the type, and requires C++20.
- The rule says to avoid *redundant* repetition. Writing the type when it is not
  redundant is not a violation.

## Enforcement

Flag redundant repetition of type names in a declaration.

## Related

- [0174](0174-es-3-dont-repeat-yourself-avoid-redundant-code.md) — this is that
  rule applied to type names.
- [0186](0186-es-23-prefer-the-initializer-syntax.md) — the `auto x{1}` versus
  `auto x = {1}` distinction belongs to both records.
- [0180](0180-es-10-declare-one-name-only-per-declaration.md) — both rules are
  about making a declaration read predictably.
