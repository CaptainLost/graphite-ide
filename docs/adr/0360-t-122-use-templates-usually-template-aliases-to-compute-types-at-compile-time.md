# 0360. T.122: Use templates (usually template aliases) to compute types at compile time

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Template metaprogramming is the only directly supported and half-way principled
way of generating types at compile time. When a type must be selected or
derived — picking a small-object representation over a large-object one,
choosing an unsigned integer wide enough for a bound, mapping an element type
to its iterator — the computation is a template, and the readable form of that
template is an alias.

The older "traits" technique, where a class template carries a nested
`::type` and every use is spelled `typename Traits<T>::type`, has mostly been
replaced: template aliases compute types, and `constexpr` functions compute
values.

Upstream leaves this rule's example unwritten, sketching only "big object /
small object optimization", so no worked example can be transcribed.

## Decision

Express a compile-time type computation as a template, and give it an alias.

- Write `template<class T> using X = ...;` rather than requiring callers to
  spell `typename X<T>::type`.
- Use the standard library's `_t` aliases — `std::conditional_t`,
  `std::remove_cvref_t` and the rest — rather than the class templates behind
  them.
- Keep the class template where partial specialisation is needed, and put the
  alias in front of it, since an alias template cannot itself be specialised.
- Use a `constexpr` function, not a type computation, when the answer is a
  value.
- Name the alias for what the resulting type means, not for the computation
  that produced it.

## Rationale

An alias is the only form in which a type computation reads as a type. The
`typename ...::type` spelling forces every use site to restate that the result
is a type, which is noise in the ordinary case and a source of hard errors in a
dependent context. The alias also hides which of the two mechanisms — a
specialisation, a conditional, a fold — produced the answer, so the
implementation can change without touching callers.

## Consequences

### Positive

- Use sites read as declarations rather than as metaprogramming.
- The `typename` disambiguator disappears from dependent contexts.
- The computation can be reimplemented behind the alias without breaking
  callers.

### Negative

- Alias templates cannot be specialised, so any computation that needs partial
  specialisation must keep a class template behind the alias — two entities
  where the design wanted one.
- Aliases are transparent, so they do not participate in deduction as distinct
  types: a template cannot deduce `T` from an argument declared through an
  alias that erases it.
- Type computations are the part of C++ with the worst diagnostics; a mistake
  is reported as a substitution failure in a context the reader did not write.
- An alias template names no entity of its own, so unlike a class template
  specialisation its use is re-substituted rather than remembered; the saving
  over a traits class is smaller than it looks, and the cost accumulates
  silently in headers.

### Neutral

- This rule concerns types; the value case belongs to the `constexpr` rule, and
  the two are stated together upstream.
- Standard traits remain the raw material; the rule is about the interface put
  on top of them.

## Related

- [0361](0361-t-123-use-constexpr-functions-to-compute-values-at-compile-time.md)
  — the value half of the same guidance.
- [0362](0362-t-124-prefer-to-use-standard-library-tmp-facilities.md) — where
  the aliases should come from before you write your own.
- [0358](0358-t-120-use-template-metaprogramming-only-when-you-really-need-to.md)
  — the restraint that governs how much of this to do.
