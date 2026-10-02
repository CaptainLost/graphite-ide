# 0358. T.120: Use template metaprogramming only when you really need to

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section T (Templates and generic programming), transcribed 2026-08-10

## Context

Template metaprogramming is hard to get right, slows down compilation, and is
often very hard to maintain. Metaprogramming here means programming where at
least one input or one result is a type; templates offer Turing-complete
duck typing at compile time, and the syntax and techniques needed are pretty
horrendous.

Against that, there are real-world examples where template metaprogramming
gives better performance than any alternative short of expert-level assembly,
and real-world examples where it expresses the fundamental idea better than
run-time code could. If you genuinely need syntax tree manipulation at compile
time — folding matrix operations, for instance — there may be no other way in
C++.

The canonical bad case is `enable_if`. Use concepts instead; concepts are a
language feature in C++20, and the metaprogrammed emulation of them is only for
compilers that do not support them.

Where the result is a value rather than a type, a `constexpr` function is the
alternative, and it is the conventional one.

If you feel the need to hide your template metaprogramming in macros, you have
probably gone too far.

## Decision

Reach for the ordinary mechanism first, and use template metaprogramming only
when nothing else expresses the requirement.

- Use concepts to constrain templates; do not write `enable_if` in new code.
- Use a `constexpr` function when the result is a value.
- Use a template alias when the result is a type, in preference to a hand-rolled
  traits class with a nested `::type`.
- Prefer the standard library's facilities to your own, and an established
  library to either, before writing new metaprogramming.
- Treat the need to hide metaprogramming behind macros as evidence the design
  has gone wrong, not as a solution.

## Rationale

The costs of metaprogramming are certain and continuous — compile time on every
build, diagnostics that name instantiations rather than mistakes, and a
maintenance burden that falls on whoever inherits the code — while the benefits
are real but rare and specific. The rule is a default, and the honest form of
the default is that C++20's concepts and `constexpr` functions have absorbed
most of the cases that once required metaprogramming.

## Consequences

### Positive

- Compile times stay proportional to the code rather than to the cleverness.
- Diagnostics point at the call that is wrong, when constraints are expressed
  as concepts.
- The code remains readable by people who are not metaprogramming specialists,
  which is most of them.

### Negative

- "Really need to" is a judgement with no test attached, so the rule cannot be
  enforced and upstream offers no enforcement for it.
- Avoiding compile-time computation sometimes means paying at run time instead,
  which conflicts directly with the guidance to move computation from run time
  to compile time.
- Libraries you depend on may impose metaprogramming on you regardless; the
  rule governs what you write, not what you must read.
- In exactly the hard cases where metaprogramming is the right answer, this
  rule gives no help at all, since its whole content is a discouragement.

### Neutral

- Upstream's own examples for this rule are unwritten, so it reads as an
  attitude rather than a technique.
- Concepts and `constexpr` are themselves compile-time programming; the rule is
  about the difficult, type-level variety.

## Related

- [0361](0361-t-123-use-constexpr-functions-to-compute-values-at-compile-time.md)
  — the alternative when the result is a value.
- [0360](0360-t-122-use-templates-usually-template-aliases-to-compute-types-at-compile-time.md)
  — the disciplined form when the result really is a type.
- [0359](0359-t-121-use-template-metaprogramming-primarily-to-emulate-concepts.md)
  — the one use upstream endorses, and the one C++20 retires.
- [0028](0028-i-9-if-an-interface-is-a-template-document-its-parameters-using-concepts.md)
  — concepts, which absorb most of what `enable_if` was used for.
