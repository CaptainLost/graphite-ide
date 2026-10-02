# 0344. T.64: Use specialization to provide alternative implementations of class templates

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

A template defines a general interface. Specialization offers a powerful
mechanism for providing alternative implementations of that interface: the
primary template says what the abstraction is, and a specialization says how it
is realised for a particular argument, without either the name or the interface
changing at the call site.

Upstream gives this rule a reason and nothing else — its example, note and
enforcement sections are placeholders — so the reasoning above is the whole of
what can be transcribed. The related rules carry the worked examples: tag
dispatch for selecting between function implementations, and the prohibition on
specializing function templates, which is what makes class template
specialization the technique of choice.

## Decision

When one template argument needs a different implementation, specialize the
class template rather than change the primary template.

- Keep the specialization's interface identical to the primary template's; a
  specialization that adds or drops operations is a different abstraction
  wearing the same name.
- Prefer a partial specialization over a full one where the alternative applies
  to a family of arguments rather than to a single type.
- Declare the primary template before any specialization, and make every
  specialization visible everywhere the template is used for those arguments.
- Where the alternative belongs to a function, route it through a class
  template you can specialize, rather than specializing the function template.

## Rationale

Specialization is the only mechanism that lets the implementation vary while
the name, the interface and every call site stay fixed — an `if` on the type
inside the primary template would work, but it forces every branch to compile
for every argument. Because the choice is made by the compiler from the
argument alone, there is no run-time dispatch and no way for a caller to select
the wrong implementation.

## Consequences

### Positive

- Callers are unaffected: the same name and the same syntax select whichever
  implementation fits their argument.
- Alternative implementations can be added after the fact, including for types
  the primary template's author never saw.
- No run-time cost, since selection happens during instantiation.

### Negative

- The primary template's contract is not checked against the specialization.
  Nothing requires them to agree on behaviour, or even on which members exist,
  so a specialization can silently be a different type.
- Specializations are subject to the one-definition rule across translation
  units: one that is visible in some units and not others is ill-formed with no
  diagnostic required, and the symptom appears far from the cause.
- Every specialization duplicates whatever it does not delegate, so the shared
  parts drift apart under maintenance.
- Upstream supplies no example, so the boundary between this rule and tag
  dispatch is left to judgement.

### Neutral

- Specialization is a compile-time mechanism; a run-time choice between
  implementations is a different design and belongs to the hierarchy rules.
- Constraining the primary template with concepts and specializing it are
  complementary, not alternatives.

## Related

- [0367](0367-t-144-dont-specialize-function-templates.md) — the reason
  alternatives are routed through class templates.
- [0345](0345-t-65-use-tag-dispatch-to-provide-alternative-implementations-of-a-function.md)
  — the corresponding technique for functions.
- [0346](0346-t-67-use-specialization-to-provide-alternative-implementations-for-irregular-types.md)
  — the same technique applied to types that do not fit the general case.
