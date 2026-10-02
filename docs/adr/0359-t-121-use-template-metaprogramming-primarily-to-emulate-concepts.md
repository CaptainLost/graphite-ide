# 0359. T.121: Use template metaprogramming primarily to emulate concepts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Where C++20 is not available, concepts have to be emulated with template
metaprogramming. The use cases that require concepts — overloading on a
requirement, in particular — are among the most common and the simplest uses of
the technique, which is why the guidelines single them out as the place
metaprogramming earns its cost.

```cpp
template<typename Iter>
    /*requires*/ enable_if<random_access_iterator<Iter>, void>
advance(Iter p, int n) { p += n; }

template<typename Iter>
    /*requires*/ enable_if<forward_iterator<Iter>, void>
advance(Iter p, int n) { assert(n >= 0); while (n--) ++p; }
```

The same code is much simpler with concepts:

```cpp
void advance(random_access_iterator auto p, int n) { p += n; }
void advance(forward_iterator auto p, int n) { assert(n >= 0); while (n--) ++p; }
```

Concepts are a language feature in C++20, so wherever that standard is
available the emulation is not needed. The rule still applies, but in its
negative form: if metaprogramming is being written to say what a template
requires, a concept says it instead.

## Decision

Treat "emulating concepts" as the one endorsed use of template
metaprogramming, and do not emulate what the language provides.

- Express requirements as named concepts and constrain templates with them.
- Do not write new `enable_if` where concepts are available; replace it with a
  `requires` clause or a constrained `auto` parameter when touching code that
  has it.
- Where a trait must be computed to feed a concept, keep it small, name it, and
  prefer a standard trait to a hand-written one.
- Where metaprogramming is used for something other than expressing a
  requirement, treat that as the case needing justification.

## Rationale

The two `advance` overloads show the whole argument: the emulated form buries
the requirement in the return type, and the concept form puts it where the
parameter is declared. Beyond readability, the constrained overloads are
ordered by subsumption, so the compiler picks the more specific one, whereas
`enable_if` requires the alternatives to be made mutually exclusive by hand —
and an incomplete set of conditions produces an ambiguity, not a diagnosis.

## Consequences

### Positive

- Requirements appear in the declaration, where a caller reads them.
- Overload resolution orders constrained candidates automatically, removing a
  class of hand-maintained mutual exclusions.
- Failures are reported against the call rather than against an instantiation
  deep inside the library.

### Negative

- Code inherited from before C++20, and third-party headers, will keep using
  the emulation, so both idioms remain readable knowledge.
- The rule as stated implies metaprogramming is acceptable for this purpose,
  which under C++20 is precisely the case where it should not be used at all;
  the rule has been overtaken by the language.
- Writing good concepts is its own skill, and a badly chosen concept constrains
  an interface wrongly in a way that is harder to unpick than an `enable_if`.
- Upstream offers no enforcement for this rule.

### Neutral

- Standard type traits remain useful as the building blocks of concepts; the
  rule targets hand-rolled dispatch, not `<type_traits>`.
- Tag dispatch is the other classic emulation and is retired for the same
  reason.

## Related

- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — the rule that says to express requirements as concepts in the first place.
- [0358](0358-t-120-use-template-metaprogramming-only-when-you-really-need-to.md)
  — the general restraint this rule is the main exception to.
- [0345](0345-t-65-use-tag-dispatch-to-provide-alternative-implementations-of-a-function.md)
  — the other emulation of constrained overloading.
