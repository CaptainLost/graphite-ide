# 0347. T.68: Use {} rather than () within templates to avoid ambiguities

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Parentheses are vulnerable to grammar ambiguities. Inside a template, where the
types involved are not known while the definition is being read, the ambiguity
cannot be resolved by looking at the code.

```cpp
template<typename T, typename U>
void f(T t, U u)
{
    T v1(T(u));    // mistake: oops, v1 is a function not a variable
    T v2{u};       // clear:   obviously a variable
    auto x = T(u); // unclear: construction or cast?
}

f(1, "asdf"); // bad: cast from const char* to int
```

The first declaration is the most vexing parse: `v1` is a function taking a `T`
and returning a `T`, not a variable. The third line is worse in a template than
elsewhere, because `T(u)` is a construction for some arguments and a cast for
others; instantiated with `T = int` and `U = const char*` it is a cast from
`const char*` to `int`, which the author of `f` did not ask for and no reader
of `f` can see.

## Decision

Inside a template definition, initialise with braces.

- Write `T v{u};` rather than `T v(u);` when declaring a variable of dependent
  type.
- Write `T{u}` rather than `T(u)` when constructing a temporary, so that a
  narrowing or otherwise surprising conversion is an error rather than a
  silently accepted cast.
- Where a conversion really is intended, use a named cast so that the intent is
  visible.
- Use `T v{};` for value initialisation rather than `T v();`, which declares a
  function.

## Rationale

Braces remove the ambiguity at the point of writing rather than at the point of
instantiation: `T v{u}` cannot be parsed as a declaration of a function, and
`T{u}` cannot quietly become a functional-style cast. In a template the payoff
is larger than in ordinary code, because the meaning of `T(u)` depends on
arguments that are chosen elsewhere, so the same source line can be a
construction in one instantiation and a lossy conversion in the next.

## Consequences

### Positive

- The most vexing parse cannot occur.
- A construction that would narrow is rejected instead of accepted, and it is
  rejected in the instantiation that does it.
- The reader of a template definition can tell a declaration from a call
  without knowing the arguments.

### Negative

- Braces are not a drop-in replacement. A type with an `initializer_list`
  constructor prefers it: `std::vector<int> v{3}` is one element, `v(3)` is
  three, and inside a template the type may be the caller's choice.
- Where a template must construct an arbitrary `T` from arbitrary arguments,
  neither form is right for every `T`, which is why the standard library
  forwards to placement `new` with parentheses in some places and braces in
  others.
- The rule bans a spelling that is correct and idiomatic in the many cases
  where no ambiguity exists, so it costs some familiarity for a hazard that
  bites rarely.

### Neutral

- Outside templates the same preference applies for other reasons; this rule is
  the template-specific part of the argument.
- Aggregate initialisation, which has no parenthesised form before C++20,
  already forces braces in some of these positions.

## Enforcement

- Flag `()` initializers.
- Flag function-style casts.

## Related

- [0186](0186-es-23-prefer-the-initializer-syntax.md) — the general preference
  for the brace form, with the non-template reasoning.
- [0205](0205-es-48-avoid-casts.md) — `T(u)` inside a template is a cast that
  nobody wrote deliberately.
- [0183](0183-es-20-always-initialize-an-object.md) — `T v{};` is also the
  shortest way to satisfy that rule for a dependent type.
