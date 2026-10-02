# 0184. ES.21: Don't introduce a variable (or constant) before you need to use it

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The reason is readability, and limiting the region of code in which the variable
can be used.

```cpp
int x = 7;
// ... no use of x here ...
++x;
```

The distance between the declaration and the first use is the whole problem.
Everything in between is code the reader must hold `x` in mind through, and code
in which a future edit may reach `x` — for a purpose unrelated to the one it was
declared for. Neither cost is visible in a diff; both grow as the gap grows.

The rule applies to constants as well as variables. A `constexpr` declared forty
lines from its use is not dangerous, but it still makes the reader carry it.

This rule is the companion of
[0175](0175-es-5-keep-scopes-small.md): that record narrows the region a name is
*visible* in, this one shortens the distance between the declaration and the
first *use* within that region.

## Decision

Declare a name immediately before the code that uses it.

- Move a declaration down to its first use rather than grouping declarations at
  the top of a function.
- Do not declare a variable in anticipation of a branch that might need it;
  declare it inside the branch.
- Where the first use is in a loop and the value is per-iteration, declare it
  inside the loop body.
- Where a declaration cannot be moved down because several later blocks need it,
  take that as a sign the function is doing several things, and consider
  splitting it.
- Apply the same to constants: a local `constexpr` belongs next to the code that
  uses it, not at the top of the file, unless it is genuinely shared.

## Rationale

A C-era habit — all declarations at the top of the block — survives in code long
after the language stopped requiring it, and it is the main thing this rule is
aimed at. The declaration-at-top style forces every reader to build a mental
symbol table before reaching any logic, and it guarantees the maximum possible
window in which a used-before-set or a repurposing can occur. Declaring at first
use makes that window as small as the code allows, without any restructuring.

## Consequences

### Positive

- A reader meets each name at the moment it becomes relevant, and never carries
  more than the current step needs.
- The window between declaration and use — where an accidental read or an
  unrelated reuse can occur — shrinks to nothing.
- A declaration that cannot be moved down flags a function that should be split.

### Negative

- Declarations scattered through a long function are harder to enumerate than a
  block of them at the top, which some readers and some debugging workflows
  prefer.
- The rule pulls against grouping conceptually related declarations together,
  which is sometimes the clearer presentation.
- "Distant from its first use" has no threshold, so enforcement is heuristic and
  will disagree with reasonable code.
- Moving a declaration down changes when its constructor runs, which is
  observable for a type with side effects — a lock, a timer, a file handle.

### Neutral

- The rule constrains position, not scope; a declaration can satisfy this and
  still be in too wide a scope, which
  [0175](0175-es-5-keep-scopes-small.md) covers.
- Class members are not affected; this is about declarations in a block.

## Enforcement

Flag declarations that are distant from their first use.

## Related

- [0175](0175-es-5-keep-scopes-small.md) — the same concern expressed as scope
  rather than distance.
- [0185](0185-es-22-dont-declare-a-variable-until-you-have-a-value-to-initialize-it-with.md)
  — the stronger form: not merely near a use, but with a value.
- [0183](0183-es-20-always-initialize-an-object.md) — declaring late is what
  makes initialising at declaration possible.
- [0189](0189-es-26-dont-use-a-variable-for-two-unrelated-purposes.md) — the
  misuse that a long gap between declaration and use invites.
