# 0133. C.168: Define overloaded operators in the namespace of their operands

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The reasons are readability, the ability to find operators using
argument-dependent lookup, and avoiding inconsistent definitions in different
namespaces.

```cpp
struct S { };
bool operator==(S, S);   // OK: in the same namespace as S, and even next to S

S s;
bool x = (s == s);
```

This is what a default `==` would do, if we had such defaults. The same holds
inside a namespace, where argument-dependent lookup is what finds the operator:

```cpp
namespace N {
    struct S { };
    bool operator==(S, S);   // OK: in the same namespace as S, and even next to S
}

N::S s;
bool x = (s == s);  // finds N::operator==() by ADL
```

The bad case is an operator defined somewhere its operand type knows nothing
about:

```cpp
struct S { };
S s;

namespace N {
    bool operator!(S a) { return true; }
    bool not_s = !s;
}

namespace M {
    bool operator!(S a) { return false; }
    bool not_s = !s;
}
```

Here the meaning of `!s` differs in `N` and in `M`. This can be most confusing.
Remove the definition in namespace `M` and the confusion is replaced by an
opportunity to make the mistake.

If a binary operator is defined for two types that are defined in different
namespaces, this rule cannot be followed. For example:

```cpp
Vec::Vector operator*(const Vec::Vector&, const Mat::Matrix&);
```

This might be something best avoided. The rule is a special case of the more
general one that helper functions should be defined in the same namespace as
their class.

## Decision

Define an overloaded operator in the namespace of the type it operates on.

- Put an operator in the same namespace as its operand type, and preferably in
  the same header, next to the type.
- Do not define an operator for a type in a namespace that type does not belong
  to, including the global namespace when the type is not global.
- Where a binary operator involves types from two namespaces, treat that as a
  design smell first; if the operation must exist, put it where the dependency
  already runs and document the choice.
- Consider defining the operator as a hidden friend inside the class body, which
  places it in the enclosing namespace for lookup purposes and keeps it next to
  the type.

## Rationale

An operator is part of a type's interface whether or not it is declared inside
the class, and this rule is what makes that true in practice: argument-dependent
lookup finds an operator by its operands, so an operator in the operand's
namespace travels with the type into every context, including generic code that
cannot name the namespace. The `!s` example shows the alternative outcome — not
a compile error but two different answers for the same expression in two
translation contexts, which is as close to per-namespace truth as C++ gets.

## Consequences

### Positive

- One meaning for the operator everywhere, because there is one place it can be
  defined.
- Generic code finds the operator without qualification or a `using`
  declaration.
- Reading the type's header shows its operators, so the interface is where the
  reader looks for it.

### Negative

- Operators for a third-party type cannot be added in one's own namespace
  without violating this rule, so adapting a type one does not own has no
  compliant answer other than a wrapper type.
- Mixed-namespace binary operators — the matrix-times-vector case — have no
  correct home at all, and the rule offers only the advice to avoid them.
- Defining an operator next to its type couples the type's header to whatever
  the operator needs, which can pull dependencies into a header that had none.
- Argument-dependent lookup, which the rule relies on, drags in every associated
  namespace and can turn an unrelated same-named function into a viable
  candidate.

### Neutral

- The rule places the operator; whether it should be a member or a non-member is
  [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md).
- It is a special case of the general convention for helper functions, and
  applying it to non-operator helpers is reasonable but out of this record's
  scope.

## Enforcement

- Flag operator definitions that are not in the namespace of their operands.

## Related

- [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md) — the
  rule that produces the non-members this one places.
- [0130](0130-c-165-use-using-for-customization-points.md) — the same
  argument-dependent lookup machinery, used deliberately.
- [0125](0125-c-160-define-operators-primarily-to-mimic-conventional-usage.md) —
  states that non-member operators should be friends or in the operands'
  namespace.
- [0020](0020-i-1-make-interfaces-explicit.md) — an operator elsewhere is part
  of the interface without appearing in it.
