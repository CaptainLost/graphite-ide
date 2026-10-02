# 0200. ES.43: Avoid expressions with undefined order of evaluation

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

You have no idea what such code does. Even if it does something sensible on the
compiler in front of you, it might do something different on another compiler,
on the next release of the same compiler, or at a different optimiser setting.

```cpp
v[i] = ++i;   //  the result is undefined
```

C++17 tightened the rules for order of evaluation: left to right, except right
to left in assignments, and the order of evaluation of function arguments
remains unspecified. That closed some of these holes but not all of them, and
the guidelines add a warning that code gets copied into older translation units
and older projects, so being clever about the exact sequencing rules of one
standard version is a poor bet.

A good rule of thumb is that you should not read a value twice in an expression
where you write it.

## Decision

Do not write an expression whose result depends on the order in which its
subexpressions are evaluated.

- Never modify an object more than once in a single expression.
- Never read an object in an expression that also writes it, unless the read is
  part of the write itself, as in `i = i + 1` or `++i` alone.
- Split the offending expression into separate statements, with the write on its
  own line, rather than reaching for a sequencing rule to justify the compact
  form.
- Do not rely on the C++17 sequencing guarantees to keep an otherwise
  order-sensitive expression correct; write the version that would be correct
  under any order.
- The order of evaluation of function arguments stays unspecified even in C++20;
  that case is
  [0201](0201-es-44-dont-depend-on-order-of-evaluation-of-function-arguments.md).

## Rationale

Undefined behaviour is not a wrong answer, it is the absence of any answer, and
an optimiser is entitled to assume it never happens. That makes this class of
bug uniquely nasty: it typically works in a debug build, works on the machine
where it was written, and changes behaviour when something unrelated is
recompiled.

The rule of thumb is what makes the rule usable. "Do not read a value twice in
an expression where you write it" is a syntactic test a person can apply while
typing, without knowing which of the sequencing rules changed in which standard
version.

## Consequences

### Positive

- Removes a class of defect that testing does not reliably find, because the
  observed behaviour depends on the toolchain rather than the input.
- The resulting code means the same thing under C++14, C++17 and C++20, so it
  survives being moved between projects.
- The rule of thumb is checkable by eye, which very few undefined-behaviour
  rules are.

### Negative

- The rule of thumb is deliberately conservative and purely syntactic, so it
  catches well-defined expressions too. `a[i] = i + 1` reads `i` on both sides
  of an assignment and looks like the forbidden shape, although nothing modifies
  `i` and the expression is fine; a reviewer applying the heuristic literally
  will ask for a rewrite that gains nothing.
- Under C++20 several of the classic examples are merely unspecified rather than
  undefined, which weakens the argument in practice and invites the response
  that the rule is obsolete. It is not — unspecified still means two valid
  results — but the distinction has to be re-explained each time.
- The rule offers no help with the harder version of the problem, where the two
  accesses are hidden behind function calls or overloaded operators and no
  syntactic pattern is visible.

### Neutral

- Sequencing within an expression is unrelated to thread-level data races; this
  record is about a single thread evaluating one expression.
- The pre-C++17 sequencing rules matter only where code must also compile as an
  older standard; the guidance is the same either way.

## Enforcement

Can be detected by a good analyser.

## Related

- [0201](0201-es-44-dont-depend-on-order-of-evaluation-of-function-arguments.md)
  — the part of the problem C++17 did not fix.
- [0197](0197-es-40-avoid-complicated-expressions.md) — `x = x++ + x++ + ++x`
  appears there too, as the extreme end of complexity.
- [0013](0013-p-7-catch-run-time-errors-early.md) — undefined behaviour is the
  case where "early" means "never", since the error may never surface as one.
