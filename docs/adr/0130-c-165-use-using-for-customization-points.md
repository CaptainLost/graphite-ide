# 0130. C.165: Use using for customization points

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reason is to find function objects and functions defined in a separate
namespace that "customize" a common function.

Consider `swap`. It is a general standard-library function with a definition
that will work for just about any type. However, it is desirable to define
specific `swap()`s for specific types: the general `swap()` will copy the
elements of two vectors being swapped, whereas a good specific implementation
will not copy elements at all.

```cpp
namespace N {
    struct X { /* ... */ };
    void swap(X&, X&);   // optimized swap for N::X
    // ...
}

void f1(N::X& a, N::X& b)
{
    std::swap(a, b);   // probably not what we wanted: calls std::swap()
}
```

The `std::swap()` in `f1()` does exactly what it was asked to do: it calls the
`swap()` in namespace `std`. Unfortunately, that is probably not what was
wanted. How is `N::X`'s own version considered?

```cpp
void f2(N::X& a, N::X& b)
{
    swap(a, b);   // calls N::swap
}
```

But that might not be what is wanted for generic code. There, the specific
function is wanted if it exists and the general function if not. This is done by
including the general function in the lookup for the function:

```cpp
void f3(N::X& a, N::X& b)
{
    using std::swap;  // make std::swap available
    swap(a, b);        // calls N::swap if it exists, otherwise std::swap
}
```

## Decision

Call a customisation point through an unqualified call preceded by a
`using` declaration of the general version.

- Write `using std::swap;` and then `swap(a, b)`, not `std::swap(a, b)`, in any
  code that must work for types it does not know.
- Apply the same form to the other customisation points reached this way, such
  as `begin`, `end` and `size` in generic code.
- Confine the `using` declaration to the smallest scope that needs it — the
  function body — rather than putting it at namespace scope.
- Define a type's own customisation, such as its `swap`, in the same namespace
  as the type, so that argument-dependent lookup can find it.
- Keep qualified calls where the general version is specifically what is
  intended, and where a type-specific override would be wrong.

## Rationale

The two-step form is the only way to express "the best available
implementation": a qualified call names one implementation and forecloses every
other, while a bare unqualified call requires that some implementation be
findable by argument-dependent lookup and fails if none is. The `using`
declaration puts the general version into the candidate set without removing the
specific one, so overload resolution — rather than the author of the generic
code, who cannot know the type — decides. That the correct form takes two lines
and looks redundant is why the rule needs writing down.

## Consequences

### Positive

- Generic code gets a type's optimised implementation where one exists and a
  working default where none does, with no traits table to maintain.
- A type opts in by defining a function next to itself, with no coupling back to
  the generic code.
- The mechanism is the one the standard library already uses, so types written
  this way work with standard algorithms.

### Negative

- The idiom is easy to get wrong in a way that still compiles: a qualified call
  silently loses the customisation, and the loss shows up as a performance
  problem rather than an error.
- It is genuinely obscure, and the `using` line looks removable to anyone who
  does not know the rule.
- It relies on argument-dependent lookup, which drags every associated namespace
  into the candidate set and can select an unrelated function of the same name.
- The check is not automatable in general, because qualified and unqualified
  lookup both have legitimate uses.

### Neutral

- C++20 offers customisation point objects, which encapsulate this dance behind
  a callable and avoid the `using` line; for the standard library's own points,
  `std::ranges::swap` and friends do this already.
- The rule addresses how a customisation point is called; whether an operation
  should be one is a separate design question.

## Enforcement

- Unlikely, except for known customisation points such as `swap`. The problem is
  that unqualified and qualified lookups both have uses.

## Related

- [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md)
  — the same argument-dependent lookup mechanism, applied to operators.
- [0127](0127-c-162-overload-operations-that-are-roughly-equivalent.md) — a
  customisation point is an overload set spanning types.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — generic code is where this idiom is needed, and concepts state what it
  requires.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — the general
  implementation being brought into scope is the library's.
