# 0185. ES.22: Don't declare a variable until you have a value to initialize it with

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Four reasons are given: readability; limiting the scope in which a variable can
be used; not risking used-before-set; and the fact that initialisation is often
more efficient than assignment.

```cpp
string s;
// ... no use of s here ...
s = "what a waste";
```

The default construction builds an empty string that is immediately discarded,
and the gap in between is a region in which `s` is live and meaningless.

The more serious form:

```cpp
SomeLargeType var;  // Hard-to-read CaMeLcAsEvArIaBlE
if (cond)   // some non-trivial condition
    Set(&var);
else if (cond2 || !cond3) {
    var = Set2(3.14);
}
else {
    var = 0;
    for (auto& e : something)
        var += e;
}
// use var; that this isn't done too early can be enforced statically with only control flow
```

This would be fine if `SomeLargeType` had a default initialisation that was not
too expensive. Otherwise, a programmer reading it may reasonably wonder whether
every possible path through the maze of conditions has been covered — and if one
has not, there is a use-before-set bug. Either way it is a maintenance trap: a
fourth branch added later has no obligation to assign anything.

For initialisers of moderate complexity, including for `const` variables, a
lambda can express the initialiser directly; that is
[0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md).

## Decision

Do not write a declaration whose initialiser you do not yet have.

- Where a value is produced by a branch, produce it: compute it in a function or
  an immediately invoked lambda that returns it, and initialise the variable
  from that call.
- Where a value is produced by a loop, accumulate into a local inside a lambda
  and return the result, rather than declaring the target before the loop.
- Do not default-construct and then assign. If the type has an expensive default
  constructor, that is waste; if it has a cheap one, the assignment is still a
  second statement the reader must find.
- Do not use an out-parameter to fill a previously declared variable when the
  function could return the value instead.
- If the initialiser genuinely cannot be formed — an object filled from a device
  or an API that only writes through a reference — keep the declaration adjacent
  to the call that fills it and nothing in between.

## Rationale

The difference between this rule and
[0183](0183-es-20-always-initialize-an-object.md) is where the burden falls: that
rule says a declaration must have a value, this one says that if you have not
got a value you have not yet earned the declaration. Stated that way it removes
the temptation to satisfy the initialisation rule with a placeholder. The
control-flow example is the argument: whether it is correct depends on an
exhaustiveness property of a chain of conditions that no reader can check by
inspection and no compiler will check at all, and initialising from a single
expression makes the question disappear rather than answering it.

## Consequences

### Positive

- A wasted default construction followed by an assignment stops appearing.
- Branch-exhaustiveness questions vanish: a function or lambda that must return
  a value cannot fall off the end without a diagnostic.
- Declaration and value arrive together, which is the precondition for making
  the variable `const`.

### Negative

- Values genuinely produced by an API that writes through a pointer or reference
  — the usual shape of a C interface — cannot follow the rule, and those are
  exactly the cases where uninitialised data is most dangerous.
- The lambda or helper function the rule pushes towards is more syntax than an
  assignment, and an immediately invoked lambda is an idiom not every reader
  finds transparent.
- Where a variable is filled in a loop across several iterations, hoisting the
  whole loop into an initialiser can obscure the loop rather than clarify it.
- Enforcement is partly heuristic; "a complicated computation after an
  uninitialised variable" is a shape, not a definition.

### Neutral

- This is a strictly stronger statement of
  [0183](0183-es-20-always-initialize-an-object.md); nothing satisfies this rule
  and violates that one.
- Reusing a deliberately hoisted buffer across loop iterations to avoid
  reallocation is a separate, permitted optimisation.

## Enforcement

- Flag declarations with default initialisation that are assigned to before they
  are first read.
- Flag any complicated computation occurring after an uninitialised variable and
  before its first use.

## Related

- [0183](0183-es-20-always-initialize-an-object.md) — the weaker rule this one
  strengthens.
- [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md)
  — the positional counterpart.
- [0191](0191-es-28-use-lambdas-for-complex-initialization-especially-of-const-variables.md)
  — the mechanism this rule depends on for non-trivial initialisers.
