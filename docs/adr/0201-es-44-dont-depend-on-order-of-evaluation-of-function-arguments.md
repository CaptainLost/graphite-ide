# 0201. ES.44: Don't depend on order of evaluation of function arguments

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The order in which the arguments of a call are evaluated is unspecified. C++17
tightened the rules for order of evaluation generally, but left this case alone:
argument evaluation order is still unspecified.

```cpp
int i = 0;
f(++i, ++i);
```

Before C++17 the behaviour was undefined, so the result could be anything,
including `f(2, 2)`. Since C++17 the code no longer has undefined behaviour, but
it is still not specified which argument is evaluated first. The call will be
`f(1, 2)` or `f(2, 1)`, and you do not know which.

Overloaded operators bring the same problem into code that does not look like a
call at all, because it is one:

```cpp
f1()->m(f2());          // m(f1(), f2())
cout << f1() << f2();   // operator<<(operator<<(cout, f1()), f2())
```

In C++17 these examples work as expected, left to right, and assignments are
evaluated right to left, matching the right-to-left binding of `=`:

```cpp
f1() = f2();    // undefined behavior in C++14; in C++17, f2() is evaluated before f1()
```

## Decision

Write calls whose arguments can be evaluated in any order without changing the
result.

- Do not modify the same object in two arguments of one call, and do not modify
  an object in one argument that another argument reads.
- Where two arguments both have side effects, evaluate them into named variables
  on separate statements first, then pass the variables.
- Treat an overloaded operator as the function call it is: a chain of `<<` whose
  operands have side effects is a nest of calls, and the operands are arguments.
- Do not perform more than one explicit resource acquisition in a single
  expression; see
  [0156](0156-r-13-perform-at-most-one-explicit-resource-allocation-in-a-single-expression-statement.md),
  which is this rule applied to ownership.

## Rationale

This is the part of the evaluation-order problem that the standard did not fix
and is not going to. Argument order is left unspecified so that implementations
can choose the most efficient calling sequence for the target, which means a
program depending on it can change behaviour between two builds of the same
source with no diagnostic anywhere.

The overloaded-operator examples are what make the rule worth stating
separately. Nobody writes `f(++i, ++i)` by accident, but `cout << f1() << f2()`
looks like a sequence of statements and is not one.

## Consequences

### Positive

- Calls behave identically across compilers, optimisation levels and calling
  conventions.
- The rewrite — evaluate to named variables first — usually improves the code
  anyway, because the intermediate values acquire names.
- Applying it to resource acquisition closes the classic leak where one argument
  allocates and another throws before the allocation is adopted by an owner.

### Negative

- Enforcement is weak in exactly the case that matters. An analyser can see
  `f(++i, ++i)`; it generally cannot see that two argument expressions call
  functions that touch the same global or the same member.
- The rewrite fights
  [0197](0197-es-40-avoid-complicated-expressions.md)'s warning against
  unnecessarily verbose one-operation-per-statement code, so applied
  over-eagerly it produces a wall of temporaries.
- Because C++17 made the classic examples merely unspecified rather than
  undefined, the code "works" consistently on any single toolchain, and the
  problem only appears when the toolchain changes — the worst possible time to
  discover it.
- The rule cannot be satisfied by inspection of the call site alone when the
  arguments are calls into another module whose side effects are not documented.

### Neutral

- Chained stream insertion, `cout << a << b`, is itself fine; only side effects
  in the operands are at issue.
- The evaluation order of the operands of most other operators was settled by
  C++17 and is covered by
  [0200](0200-es-43-avoid-expressions-with-undefined-order-of-evaluation.md).

## Enforcement

Can be detected by a good analyser.

## Related

- [0200](0200-es-43-avoid-expressions-with-undefined-order-of-evaluation.md) —
  the general rule; this is the case it excludes.
- [0156](0156-r-13-perform-at-most-one-explicit-resource-allocation-in-a-single-expression-statement.md)
  — the leak that argument order causes.
- [0197](0197-es-40-avoid-complicated-expressions.md) — an argument list with
  side effects is a complicated expression.
