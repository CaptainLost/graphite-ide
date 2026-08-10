# 0113. C.138: Create an overload set for a derived class and its bases with using

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Without a `using` declaration, member functions in the derived class hide the
entire inherited overload set. Name lookup stops at the first scope in which the
name is found, so declaring any `f` in the derived class removes every base `f`
from consideration, regardless of signature.

```cpp
// bad
#include <iostream>

class B {
public:
    virtual int f(int i) { std::cout << "f(int): "; return i; }
    virtual double f(double d) { std::cout << "f(double): "; return d; }
    virtual ~B() = default;
};

class D: public B {
public:
    int f(int i) override { std::cout << "f(int): "; return i + 1; }
};

int main()
{
    D d;
    std::cout << d.f(2) << '\n';   // prints "f(int): 3"
    std::cout << d.f(2.3) << '\n'; // prints "f(int): 3"
}
```

The second call is the damage. `d.f(2.3)` does not call `B::f(double)`; it finds
only `D::f(int)`, converts `2.3` to `2`, and prints `3`. The code compiles
without a diagnostic, and the loss of precision is invisible.

```cpp
// good
class D: public B {
public:
    int f(int i) override { std::cout << "f(int): "; return i + 1; }
    using B::f; // exposes f(double)
};
```

This issue affects both virtual and non-virtual member functions. For variadic
bases, C++17 introduced a variadic form of the using-declaration:

```cpp
template<class... Ts>
struct Overloader : Ts... {
    using Ts::operator()...; // exposes operator() from every base
};
```

## Decision

When a derived class declares a member whose name exists in a base, bring the
base's overloads in with `using`.

- Add `using Base::name;` to the derived class whenever it declares a function
  with a name the base also uses, whether or not that function overrides
  anything.
- Do this for non-virtual functions too; hiding is a name-lookup effect and does
  not depend on virtuality.
- For a class deriving from a parameter pack, use the C++17 variadic form
  `using Ts::name...;`.
- Where hiding the base overloads is deliberate, say so with a comment, or
  delete the ones that should not be callable, so that the omission of `using`
  reads as a decision rather than an oversight.

## Rationale

The failure is silent and it is a wrong answer rather than a compile error. The
derived class did nothing unusual — it overrode one of two overloads, which is a
perfectly ordinary thing to want — and the effect was to change the behaviour of
a call it never mentioned.

`using B::f;` is a one-line, zero-cost fix, and its absence is invisible: there
is nothing at the call site or in the derived class to suggest that half the
interface has gone. That asymmetry between the cost of the fix and the cost of
the bug is what makes this worth a standing rule rather than case-by-case
judgement.

## Consequences

### Positive

- The derived class offers the full inherited overload set, so callers see the
  interface they expect from the base.
- Conversions that would silently pick the wrong overload stop happening.
- Deliberate hiding becomes distinguishable from accidental hiding.

### Negative

- The overload set now spans two classes, so resolving a call means reading
  both, and adding an overload to the base can change which function a derived
  call selects.
- `using` brings in every overload of the name, including ones the derived class
  would rather not expose; it is all-or-nothing.
- The declaration must be added by hand in every derived class, and nothing
  requires it, so the rule holds only as long as everyone remembers.

### Neutral

- `using` does not make the inherited functions virtual or non-virtual; it only
  affects name lookup.
- Hiding is sometimes exactly what is wanted, and the rule accommodates that by
  asking for it to be explicit.

## Enforcement

- Diagnose name hiding.

## Related

- [0103](0103-c-128-virtual-functions-should-specify-exactly-one-of-virtual-override-or-final.md)
  — `override` catches the neighbouring case, where a mismatched signature hides
  instead of overriding.
- [0035](0035-i-24-avoid-adjacent-parameters-that-can-be-invoked-by-the-same-arguments-in-either-order-with-different-meaning.md)
  — another silent wrong-overload trap.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  `double`-to-`int` conversion is what turns hiding into a wrong answer.
