# 0447. F.15: Prefer simple and conventional ways of passing information

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Using unusual and clever techniques for passing arguments and returning results
causes surprises, slows understanding by other programmers, and encourages bugs.
If you really feel the need for an optimisation beyond the common techniques,
measure to ensure that it really is an improvement, and document or comment it,
because the improvement might not be portable.

This rule is the umbrella over the parameter-passing rules that follow it. The
upstream text summarises them in two tables: a normal parameter-passing table
covering the ordinary cases, and an advanced parameter-passing table covering
the rest. The distinction the tables draw is the point of the rule. The normal
techniques are the ones a reader should never have to think about:

- an "in" parameter is passed by value if it is cheap to copy, and by reference
  to `const` otherwise;
- an "in-out" parameter is passed by reference to non-`const`;
- an "out" value is returned, not written through a parameter.

The advanced techniques — passing by `X&&` to move from, passing by `TP&&` to
forward, adding an rvalue overload alongside a `const&` one, the
caller-allocated out pattern — exist for cases the normal ones handle badly.
Use them only after demonstrating need, and document that need in a comment.

For passing sequences of characters, the string-specific rules apply rather than
the general ones.

## Decision

Pass and return information the conventional way, and treat any departure as an
optimisation that must be justified.

- Default to the normal set: by value or by reference to `const` for input, by
  reference to non-`const` for in-out, by return value for output.
- Reach for `X&&`, `TP&&`, overload pairs, or caller-allocated out parameters
  only when a normal technique has been shown to be inadequate.
- When using an advanced technique, write a comment saying which need it serves.
- Support any performance claim with a measurement, on the configuration that
  matters, before the clever version is committed.
- Do not invent a private convention for passing information; a reader should
  recognise every signature in the codebase from the same short list.

## Rationale

Conventional signatures are readable without being read: a reviewer who sees
`const std::string&` knows the argument is input and is not modified, and spends
no attention on it. Every unconventional signature spends some of that attention
and, worse, casts doubt on the conventional ones nearby, because the reader now
knows the codebase does not follow a predictable scheme. The measurement
requirement is what keeps that cost from being paid for imaginary gains — most
of the folklore about `&&` parameters being faster is either false or
configuration-specific.

## Consequences

### Positive

- Signatures become self-documenting, because a small fixed vocabulary carries
  the in, in-out and out distinctions.
- Review effort concentrates on the unusual signatures, which are now marked by
  a comment explaining themselves.
- Callers can predict what a function does with an argument without reading its
  body.
- Fewer opportunities for the lifetime and moved-from mistakes that the advanced
  techniques invite.

### Negative

- The conventional choice is sometimes measurably slower, and this rule requires
  paying that cost until someone does the measurement to justify not paying it.
- What counts as "conventional" is a moving target: today's advanced technique
  becomes tomorrow's normal one, and the record will age.
- The rule pushes back against generic code, where forwarding references are
  both normal and unavoidable, so template-heavy code sits awkwardly under it.
- It is unenforceable by tooling; the sub-rules can be checked individually but
  "simple and conventional" cannot.

### Neutral

- The two upstream tables are summarised here rather than reproduced; the
  detailed cases live in the individual records for the rules they summarise.
- Character sequences are governed by the separate string rules, not by this
  one.

## Related

- [0448](0448-f-16-for-in-parameters-pass-cheaply-copied-types-by-value-and-others-by-reference-to-const.md)
  — the normal technique for input.
- [0450](0450-f-18-for-will-move-from-parameters-pass-by-x-and-std-move-the-parameter.md)
  — the first of the advanced techniques, and when it is warranted.
- [0452](0452-f-20-for-out-output-values-prefer-return-values-to-output-parameters.md)
  — the normal technique for output.
- [0009](0009-p-3-express-intent.md) — a conventional signature is how passing
  intent is expressed.
