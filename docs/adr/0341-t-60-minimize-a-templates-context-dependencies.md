# 0341. T.60: Minimize a template's context dependencies

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Minimising what a template definition depends on beyond its own arguments eases
understanding, minimises errors from unexpected dependencies, and eases tool
creation.

Some dependencies are necessary and useful:

```cpp
template<typename C>
void sort(C& c)
{
    std::sort(begin(c), end(c)); // necessary and useful dependency
}
```

Others are not:

```cpp
template<typename Iter>
Iter algo(Iter first, Iter last)
{
    for (; first != last; ++first) {
        auto x = sqrt(*first); // potentially surprising dependency: which sqrt()?
        helper(first, x);      // potentially surprising dependency:
                               // helper is chosen based on first and x
        TT var = 7;            // potentially surprising dependency: which TT?
    }
}
```

Each of the three surprising lines is resolved by something the author of
`algo` did not write down: an overload found by argument-dependent lookup, a
name visible only because of what the caller included, or a type name that
means whatever the surrounding scope says it means.

Templates typically appear in header files, so their context dependencies are
more vulnerable to `#include` order dependencies than functions in a `.cpp`
file are.

Having a template operate only on its arguments would be one way of reducing
the number of dependencies to a minimum, but that would generally be
unmanageable. Algorithms usually use other algorithms and invoke operations
that do not operate exclusively on their arguments. Macros make the problem
worse still, because they are not subject to scope at all.

## Decision

Keep a template definition's dependence on its surrounding context as small as
the work allows.

- Qualify calls to helper functions — `detail::helper(t)` — unless the call is
  deliberately a customization point.
- Do not rely on a name being visible because of what some other header
  happened to include; include what the definition uses.
- Prefer names introduced by the template's own arguments (member functions,
  associated types, concepts) over names looked up in the enclosing namespace.
- Keep the dependencies that are genuinely part of the abstraction, such as
  `begin`, `end` and `std::sort` in a generic `sort`, and be able to say why
  each one is there.
- Do not depend on macros inside a template definition.

## Rationale

A template is compiled in a context the author does not control: the caller's
translation unit decides which overloads are visible and in what order headers
were seen. Every unintended dependency is therefore a way for a caller to
change the meaning of code they did not write, without any diagnostic. Keeping
the dependency set small is what makes a template definition reviewable at all,
because the reader can enumerate what it could possibly bind to.

## Consequences

### Positive

- The behaviour of an instantiation can be predicted from the definition and
  its arguments, rather than from the caller's include set.
- Include-order bugs, which are notoriously hard to reproduce, largely
  disappear.
- Tooling — indexers, refactoring engines, static analysers — can resolve names
  in the definition instead of guessing.

### Negative

- Qualification is verbose, and over-qualifying suppresses the customization
  that some designs deliberately want; the `swap`/`begin` idiom depends on an
  unqualified call.
- "Minimal" is a judgement, not a threshold: upstream concedes that a template
  depending only on its arguments is generally unmanageable, so the rule cannot
  say when you have arrived.
- Upstream offers no enforcement for this rule and marks the problem tricky, so
  compliance rests on review rather than on a check.
- Reducing dependencies sometimes means passing more in — extra template
  parameters or function objects — which pushes cost onto every call site.

### Neutral

- The rule is about the definition, not the interface; constraining the
  interface with concepts is a separate decision.
- Necessary dependencies are not violations. The rule asks for them to be
  intentional, not absent.

## Related

- [0348](0348-t-69-inside-a-template-dont-make-an-unqualified-non-member-function-call-unless-you-intend-it-to-be-a-customization-point.md)
  — the specific case of unqualified calls, which is the largest source of
  surprising dependencies.
- [0342](0342-t-61-do-not-over-parameterize-members-scary.md) — the same
  instinct applied to a template's parameters rather than its body.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — stating requirements on the arguments is what lets the definition rely on
  them instead of on the context.
