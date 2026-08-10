# 0189. ES.26: Don't use a variable for two unrelated purposes

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

The reasons are readability and safety.

```cpp
void use()
{
    int i;
    for (i = 0; i < 20; ++i) { /* ... */ }
    for (i = 0; i < 200; ++i) { /* ... */ } // bad: i recycled
}
```

Nothing here is wrong yet, which is the point: the code works, and it works only
because the second loop happens to reset `i` before reading it. Remove that
reset — by changing the second loop to a `while`, or by inserting a conditional
around the initialisation — and the two uses become entangled through a value
neither loop declares an interest in. The name also stops meaning anything: `i`
is not "the index", it is "whatever index we are on at the moment".

There is a legitimate optimisation nearby. A buffer may be reused as a scratch
pad to avoid reallocating on every iteration, but even then the variable's scope
should be limited as far as possible, and care is needed not to introduce bugs
from data left in the recycled buffer — a common source of security bugs.

```cpp
void write_to_file()
{
    std::string buffer;             // to avoid reallocations on every loop iteration
    for (auto& o : objects) {
        // First part of the work.
        generate_first_string(buffer, o);
        write_to_file(buffer);

        // Second part of the work.
        generate_second_string(buffer, o);
        write_to_file(buffer);
        // etc...
    }
}
```

The distinction is that here the buffer has one purpose — scratch storage for
generating a string — used repeatedly, rather than two unrelated purposes. Its
correctness depends on each `generate_*` function fully overwriting what the
previous one left, which is a real obligation and not a free lunch.

## Decision

Give each purpose its own variable.

- Declare a fresh loop variable for each loop, in the loop's own initialiser;
  see [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md).
- Do not reuse a temporary because it happens to have the right type and is
  already in scope.
- Do not overload the meaning of a variable across phases of a function — an
  index that later becomes a count, a status that later becomes a result.
- Where a buffer is deliberately reused for performance, keep its scope as small
  as the reuse allows, and make sure every user of it writes the whole thing
  rather than appending to what was left.
- If reusing a variable seems attractive because declaring another would be
  noise, the function is probably long enough to split.

## Rationale

A variable is a name for a thing, and using one name for two things costs
exactly what it costs in prose. The safety half is sharper than the readability
half: a recycled variable carries the previous phase's value into the next one,
so a missing initialisation is not a use of garbage — which tools detect — but a
use of a plausible, well-defined, wrong value, which nothing detects. The
scratch-buffer exception has the same shape, which is why it comes with a
warning attached rather than a blessing.

## Consequences

### Positive

- Each name means one thing, so reading an expression does not require knowing
  which phase of the function it is in.
- Stale values cannot cross from one phase to the next, removing a class of bug
  that tools cannot see.
- Variables end up declared close to their use, which supports
  [0175](0175-es-5-keep-scopes-small.md) rather than fighting it.

### Negative

- The deliberate reuse case — a scratch buffer kept alive to avoid reallocation
  — looks exactly like the violation, so the rule cannot be applied mechanically
  without losing a real optimisation.
- More variables means more names, and in a long function that is its own kind
  of noise.
- "Unrelated" is a judgement about intent that no analyser can make; enforcement
  will flag legitimate reuse and miss unrelated reuse that shares a plausible
  name.

### Neutral

- Reuse across iterations of a single loop for a single purpose is not what this
  rule prohibits.
- The rule says nothing about how variables are named, only about how many there
  should be.

## Enforcement

Flag recycled variables.

## Related

- [0176](0176-es-6-declare-names-in-for-statement-initializers-and-conditions-to-limit-scope.md)
  — declaring in the initialiser makes the loop case impossible.
- [0175](0175-es-5-keep-scopes-small.md) — a variable that is out of scope cannot
  be recycled.
- [0182](0182-es-12-do-not-reuse-names-in-nested-scopes.md) — one name meaning
  two things at once, rather than one after the other.
- [0184](0184-es-21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it.md)
  — a variable introduced early is the one that gets repurposed.
