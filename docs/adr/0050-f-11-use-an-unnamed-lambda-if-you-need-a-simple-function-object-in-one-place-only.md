# 0050. F.11: Use an unnamed lambda if you need a simple function object in one place only

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

The converse of F.10 and the last rule in the F.def subsection. Together the two
rules bound the question from both sides: name it when it recurs, leave it
anonymous when it does not.

## Decision

Adopt F.11 as written. Transcribed in full:

> **Reason.** That makes the code concise and gives better locality than
> alternatives.

**Example.**

```cpp
auto earlyUsersEnd = std::remove_if(users.begin(), users.end(),
                                    [](const User &a) { return a.id > 100; });
```

**Exception.** Naming a lambda can be useful for clarity even if it is used only
once.

**Enforcement.** Look for identical and near identical lambdas (to be replaced
with named functions or named lambdas).

## Rationale

Adopted as written. Its value is that it stops F.10 from being read as "name
everything": a one-use predicate at the point of the algorithm call is the
preferred form, not a concession.

The Exception matters as much as the rule. A single-use lambda whose body needs
explaining is better named, because the name is the explanation — which is the
same argument F.1 makes about breaking up an expression into logical parts.

Taken with [0049](0049-f-10-if-an-operation-can-be-reused-give-it-a-name.md),
the working rule for this codebase is: anonymous by default at the call site;
named on the second occurrence, or on the first if the body is not
self-evident.

## Consequences

### Positive

- Keeps simple predicates next to the call that uses them, which is where they
  are read.
- Prevents F.10 from generating a drawer of single-use named helpers.

### Negative

- An anonymous lambda cannot be unit-tested, so any predicate whose correctness
  is not obvious is better off named even when used once — the Exception
  swallows a fair share of the rule.

### Neutral

- Enforcement is the same "look for near-identical lambdas" as F.10; no separate
  check.
- This completes the F.def subsection. Parameter passing continues in F.call.

## Related

- [0049](0049-f-10-if-an-operation-can-be-reused-give-it-a-name.md) — the
  previous rule, and this one's converse.
- [0040](0040-f-1-package-meaningful-operations-as-carefully-named-functions.md)
  — F.1, which opens the subsection.
