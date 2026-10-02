# 0402. SL.str.12: Use the s suffix for string literals meant to be standard-library strings

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

Direct expression of an idea minimises mistakes.

```cpp
auto pp1 = make_pair("Tokyo", 9.00);         // {C-style string,double} intended?
pair<string, double> pp2 = {"Tokyo", 9.00};  // a bit verbose
auto pp3 = make_pair("Tokyo"s, 9.00);        // {std::string,double}    // C++14
pair pp4 = {"Tokyo"s, 9.00};                 // {std::string,double}    // C++17
```

The first line is the problem. A string literal has type `const char[N]`, and
where the type is deduced that is what the deduction produces — a pointer, not a
string. The reader cannot tell from the code whether that was intended, and the
consequences are not cosmetic: the resulting object holds a pointer into static
storage, compares by address rather than by contents, and behaves differently
from the `std::string` the author probably meant.

The second line fixes the type by writing it out, which works but restates
something the initialiser already says. The third and fourth fix it at the
literal, where the intent actually is. The suffix comes from
`std::literals::string_literals` and has been available since C++14; class
template argument deduction, which makes the fourth line possible, arrived in
C++17.

## Decision

Attach the type to the literal when a `std::string` is what is meant.

- Write `"text"s` for a literal that is meant to be a `std::string`, especially
  where the type is deduced — `auto`, a template argument, a `make_` helper, or
  a braced initialiser.
- Bring the suffix into scope with a `using namespace std::string_literals;` in
  the narrowest scope that needs it, not at namespace scope in a header.
- Where the surrounding type is already spelled out, the suffix is optional;
  the rule is about deduced contexts.
- Do not write `"text"s` where a `string_view` or a plain literal is wanted; the
  suffix requests an allocation, and `sv` is the suffix for a view.

## Rationale

The mistake this prevents is silent. Deducing `const char*` where `std::string`
was meant produces code that compiles, runs, and usually appears to work, until
something compares two of them or outlives the storage. There is no diagnostic
to wait for.

Fixing it at the literal rather than at the declaration also keeps the fix where
the information is: the author knows what the literal means, and every
downstream deduction then follows from that, however many layers of templates
lie between.

## Consequences

### Positive

- Deduced types match intent, so `auto` and template argument deduction stop
  silently producing pointers where strings were meant.
- The intent is expressed once, at the literal, rather than restated in every
  declaration that carries the value.
- It removes the verbosity objection to writing the type out.

### Negative

- The suffix constructs a `std::string`, so it allocates; used carelessly in a
  hot path or a large table of literals it turns free static data into run-time
  work.
- It requires a `using` declaration to be in scope, which is a small piece of
  ceremony that has to be repeated and is easy to forget, and the failure is a
  confusing diagnostic about an undefined suffix.
- The suffix looks like part of the string to an unfamiliar reader, and is easy
  to miss in review — a single character carrying the difference between two
  types.
- There is now a family of suffixes — `s`, `sv`, and the `std::chrono` ones —
  and choosing between `s` and `sv` is a genuine decision this rule does not
  cover.

### Neutral

- Upstream gives no enforcement for this rule.
- The rule concerns literals only; it has nothing to say about strings arriving
  from anywhere else.

## Related

- [0395](0395-sl-str-1-use-std-string-to-own-character-sequences.md) — why the
  intended type is `std::string` in the first place.
- [0009](0009-p-3-express-intent.md) — the philosophy rule this is a small,
  concrete instance of.
- [0181](0181-es-11-use-auto-to-avoid-redundant-repetition-of-type-names.md) —
  the rule whose deduction this one keeps honest.
- [0398](0398-sl-str-4-use-char-to-refer-to-a-single-character.md) — what the
  unsuffixed literal actually deduces to, and why that matters.
