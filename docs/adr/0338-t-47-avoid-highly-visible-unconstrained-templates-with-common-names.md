# 0338. T.47: Avoid highly visible unconstrained templates with common names

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

An unconstrained template argument is a perfect match for anything, so such a
template can be preferred over more specific types that require minor
conversions. This is particularly annoying and dangerous when argument-dependent
lookup is involved, and common names make the problem more likely.

```cpp
namespace Bad {
    struct S { int m; };
    template<typename T1, typename T2>
    bool operator==(T1, T2) { cout << "Bad\n"; return true; }
}

namespace T0 {
    bool operator==(int, Bad::S) { cout << "T0\n"; return true; }  // compare to int

    void test()
    {
        Bad::S bad{ 1 };
        vector<int> v(10);
        bool b = 1 == bad;
        bool b2 = v.size() == bad;
    }
}
```

This prints `T0` and then `Bad`. The `==` in `Bad` was designed to cause trouble,
but the problem would be hard to spot in real code: `v.size()` returns an
unsigned integer, so a conversion is needed to call the local `==`, whereas the
`==` in `Bad` requires no conversion at all and therefore wins. Realistic types,
such as the standard-library iterators, can be made to exhibit similar
anti-social tendencies.

If an unconstrained template is defined in the same namespace as a type, that
template can be found by argument-dependent lookup, as happened above. That is
what "highly visible" means.

The rule should not be necessary, but the committee could not agree to exclude
unconstrained templates from argument-dependent lookup. Enforcement will produce
many false positives; the standard library violates the rule widely by putting
many unconstrained templates and types into the single namespace `std`.

## Decision

Do not leave a template with a common name unconstrained in a namespace that also
contains types.

- Constrain the parameters of any operator template or common-named function
  template, so that it stops being a perfect match for everything.
- Keep unavoidably unconstrained templates in a namespace of their own, or in a
  detail namespace, away from the types whose lookup would find them.
- Be especially careful with operator templates: they are found by
  argument-dependent lookup from any operand type declared alongside them, and
  the caller has no syntax to exclude them.
- When comparing values of different types, be aware that an exact match to an
  unconstrained template beats a better-intentioned overload requiring a
  conversion; fix that by constraining the template, not by adding conversions.

## Rationale

Overload resolution prefers the candidate needing no conversions, and an
unconstrained template needs none for any argument whatsoever, so it does not
merely join an overload set — it dominates it. Argument-dependent lookup then
drags that candidate into call sites the author never sees, from any namespace
that contributed an argument type, which makes the defect non-local: the
namespace defining the greedy template is not the one where the wrong answer
appears.

## Consequences

### Positive

- Overload resolution stops being decided by which candidate accidentally avoids
  a conversion.
- Argument-dependent lookup brings in only candidates that claim to handle the
  argument types.
- Errors become failures to find an overload, rather than silent selection of the
  wrong one.

### Negative

- Any check produces heavy false positives, and the standard library itself
  violates the rule, so it cannot be enforced mechanically without exemptions.
- "Common name" is a judgement with no definition; `operator==` clearly counts,
  but the boundary is unclear.
- Separating templates from types by namespace fights against
  [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md),
  which puts operators in the operands' namespace precisely so lookup finds them.
- Constraining a template that already has users is a breaking change, so the
  remedy is cheap only before release.

### Neutral

- The rule exists to compensate for a language decision — unconstrained templates
  participate in argument-dependent lookup — that upstream says should have gone
  the other way.
- It concerns visibility and constraint, not whether the template should exist.

## Enforcement

- Flag templates defined in a namespace where concrete types are also defined.

## Related

- [0321](0321-t-10-specify-concepts-for-all-template-arguments.md) — constraining
  every parameter is the direct remedy.
- [0133](0133-c-168-define-overloaded-operators-in-the-namespace-of-their-operands.md)
  — the placement rule this one is in tension with.
- [0126](0126-c-161-use-non-member-functions-for-symmetric-operators.md) — the
  non-member operators most exposed to this hazard.
- [0330](0330-t-25-avoid-complementary-constraints.md) — its recommended
  unconstrained general template is a case to watch here.
