# 0208. ES.55: Avoid the need for range checking

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Constructs that cannot overflow do not overflow, and they usually run faster.

```cpp
for (auto& x : v)      // print all elements of v
    cout << x << '\n';

auto p = find(v, x);   // find x in v
```

Neither line contains an index, a bound, or a comparison against a size. There
is nothing to get wrong, because the loop and the algorithm derive their extent
from the sequence itself rather than from a number the author supplied.

The rule is the positive form of the argument made by
[0199](0199-es-42-keep-use-of-pointers-simple-and-straightforward.md): rather
than writing an index expression and then checking it, write code in which no
index expression exists.

## Decision

Prefer formulations in which no range check is possible because no range can be
exceeded.

- Use a range-for to visit every element of a sequence rather than an index loop
  bounded by `size()`; see
  [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md).
- Use a standard algorithm — `find`, `copy`, `transform`, `accumulate` — rather
  than a hand-written loop with an index, per
  [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md).
- Pass a range type that carries its own extent rather than a pointer and a
  count; see [0157](0157-r-14-avoid-parameters-prefer-span.md).
- Where an index is genuinely needed, derive it from the container rather than
  from a separate constant, so the bound cannot fall out of step with the data.
- Do not read this rule as permission to delete a check that is part of the
  required logic. Validating an index that came from outside the program is not
  a redundant check; it is the program's job.

## Rationale

A range check is a proposition about code written elsewhere, and it is correct
only for as long as that code is unchanged. Rewriting so that the extent comes
from the sequence removes the proposition entirely — there is nothing left to
keep in sync, and no check to omit on one of the four paths through the
function.

The performance remark in the source is secondary but real: a loop whose bound
the compiler can see is a loop the compiler can vectorise or unroll, and a
checked index the optimiser cannot prove is in range is a branch that stays.

## Consequences

### Positive

- Whole categories of off-by-one and unchecked-index defects become
  unexpressible rather than merely discouraged.
- The resulting code is shorter and usually faster, because the bound is known
  to the compiler rather than recomputed.
- Fewer checks means fewer branches that are never exercised by tests, which is
  where wrong error handling hides.

### Negative

- It applies only where a suitable algorithm or range form exists. Loops that
  touch two elements at once, step by more than one, or run two sequences at
  different rates get no help from this rule and must still index.
- "Avoid the need for range checking" is one careless reading away from "avoid
  range checking", which is the opposite of the intent and considerably more
  dangerous than the code it replaced.
- Index-free code can hide cost: replacing an indexed access with a lookup by
  value trades a bounds concern for a linear search, which may be a much worse
  trade.
- The rule is stated in three lines with no discussion of the cases where it
  does not apply, so it carries less guidance than its scope demands.

### Neutral

- Where a check remains, that is not a violation of this record; the record is
  about removing the need, not about suppressing the check that a remaining need
  requires.

## Enforcement

Look for explicit range checks and heuristically suggest alternatives.

## Related

- [0217](0217-es-71-prefer-a-range-for-statement-to-a-for-statement-when-there-is-a-choice.md)
  — the loop form with no bound to check.
- [0172](0172-es-1-prefer-the-standard-library-to-other-libraries-and-to-handcrafted-code.md)
  — the algorithms that already got the bounds right.
- [0199](0199-es-42-keep-use-of-pointers-simple-and-straightforward.md) — the
  same argument stated as a prohibition.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — a
  bound the compiler knows beats a bound the program tests.
