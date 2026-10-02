# 0367. T.144: Don't specialize function templates

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

The language rules do not allow a function template to be partially
specialized. Full specialization is allowed, but it is almost never what is
wanted: an explicit specialization of a function template does not participate
in overload resolution, so it does not behave the way the author probably
expected. Overload resolution first chooses among the visible function
templates and non-template functions; only after a primary template has been
selected does the compiler look for a specialization of that particular
template. A specialization of a different primary template — one that was never
selected — is simply not considered, and adding an ordinary overload later can
change which primary wins and therefore silently strand the specialization.

Upstream gives no example for this rule; the entry's example section is left
unfilled.

There is an exception. If you do have a valid reason to specialize a function
template, write a single function template that delegates to a class template,
and specialize the class template instead. Class templates may be partially as
well as fully specialized, so the mechanism the language does support is
reached through one level of indirection.

## Decision

Add an overload rather than an explicit specialization.

- Do not write `template<> ... f<T>(...)` for a function template you control;
  declare an overload taking the type in question instead.
- Where the varying behaviour depends on a pattern rather than a single type —
  all pointers, all instances of a class template — do not attempt it with a
  function specialization at all, since partial specialization of a function
  template does not exist.
- Where specialization is genuinely required, keep a single unspecialized
  function template whose body forwards to a class template, and specialize the
  class template, fully or partially, as needed.
- Prefer tag dispatch or a constrained overload set where the alternative
  implementations are selected by a property rather than by an exact type.

## Rationale

The decisive fact is that specialization and overloading are two separate
mechanisms that look alike at the point of writing and behave differently at
the point of calling. An overload is a candidate; a specialization is not — it
is a substitute body attached to a candidate that may or may not be chosen.
That distinction is invisible in the source, which is why a specialization
usually appears to work until an overload is added elsewhere and the intended
behaviour quietly stops being selected.

## Consequences

### Positive

- Alternative implementations become ordinary overloads, resolved by rules that
  the reader already knows and that the compiler applies at every call.
- A later addition to the overload set cannot silently deactivate an
  implementation, because every implementation is a candidate.
- Behaviour that varies by pattern rather than by exact type becomes expressible
  at all, once the specialization happens on a class template.

### Negative

- The delegation workaround is real boilerplate: an extra class template, an
  extra name to keep in step with the function it serves, and an extra
  indirection for the reader to follow before reaching the code that runs.
- Overloading has its own failure mode. An overload set that grows by accretion
  produces ambiguity errors and unintended conversions where a specialization
  would at least have been unambiguous about which type it was for.
- "Overload instead" cannot be followed where the template being customized
  belongs to the standard library, because a program may not add declarations to
  `namespace std`. Customization there goes through yet another mechanism —
  specializing a standard *class* template such as `std::hash` for a
  program-defined type, or supplying a non-member function in the type's own
  namespace for lookup to find — so the rule leaves the commonest case to be
  solved some third way.
- Existing code specializes function templates, `std::swap` being the classic
  case, and blanket enforcement flags all of it at once. Applied to a codebase
  that already has such specializations, the check yields a migration rather
  than a set of local fixes.

### Neutral

- The rule is about function templates only; specializing class templates is a
  recommended technique and is what the exception routes through.
- Nothing here says how many alternative implementations are reasonable, only
  how to express them.

## Enforcement

- Flag all explicit specializations of a function template, and suggest an
  overload instead.

## Related

- [0344](0344-t-64-use-specialization-to-provide-alternative-implementations-of-class-templates.md)
  — the mechanism the exception delegates to, where specialization is the right
  tool.
- [0345](0345-t-65-use-tag-dispatch-to-provide-alternative-implementations-of-a-function.md)
  — the recommended way to select among function implementations instead.
- [0348](0348-t-69-inside-a-template-dont-make-an-unqualified-non-member-function-call-unless-you-intend-it-to-be-a-customization-point.md)
  — why customization is done through overloads found by lookup, not through
  specializations.
- [0128](0128-c-163-overload-only-for-operations-that-are-roughly-equivalent.md)
  — the discipline that keeps the resulting overload set honest.
