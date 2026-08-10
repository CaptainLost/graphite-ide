# 0419. NL.1: Don't say in comments what can be clearly stated in code

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Compilers do not read comments. Comments are less precise than code. Comments
are not updated as consistently as code.

```cpp
auto x = m * v1 + vv;   // multiply m with v1 and add the result to vv
```

The comment adds nothing the statement does not already say, and it is the half
of the line that no tool checks. When the expression changes, the comment does
not have to, so the pair silently stops agreeing.

The NL section as a whole is presented upstream as a set of suggestions — the
stated aim is consistency rather than any particular rule set. This rule is one
of the few in the section that is not a matter of taste: a comment that
paraphrases its own statement is a maintenance liability regardless of which
house style is in force.

## Decision

Do not write a comment that restates what the code beside it already says.

- Delete a comment that paraphrases the expression or statement it annotates.
- When the urge to explain what a line does arises, first try to remove the
  need: rename the variable, name the operation as a function, or introduce a
  named constant, so the code carries the explanation.
- Keep comments for what the code cannot express — intent, invariants, the
  reason an obvious alternative was rejected. Stating intent is
  [0420](0420-nl-2-state-intent-in-comments.md); this record only removes the
  duplication.
- Do not annotate a declaration whose name and type already describe it.

## Rationale

A comment that duplicates the code has no independent truth of its own, but it
can still be wrong, so it is pure downside: it costs a line, costs a reader's
attention, and eventually lies. The valuable comment is the one whose content
cannot be recovered from the code at all, and every duplicated comment makes
that one harder to spot.

## Consequences

### Positive

- Comment density becomes a signal: what is commented is what could not be
  said in code.
- Nothing is left to drift out of date except the comments that carry real
  information.
- Pressure lands where it belongs, on naming, because the alternative to a
  redundant comment is usually a better name.

### Negative

- "Clearly stated in code" is a judgement, and it is the author's judgement,
  made with the most context anyone will ever have. A reader arriving later may
  need exactly the comment the author deleted as obvious.
- Deleting comments in bulk risks removing one that recorded a subtlety and
  merely looked like a restatement — an aliasing hazard, a deliberate rounding —
  and that information is not recoverable from the diff.
- It cannot be checked mechanically, so it holds by review alone, which is the
  weakest form of rule this directory can contain.

### Neutral

- The rule says nothing about how many comments a file should have, only that
  redundant ones do not count towards the total.
- Documentation comments that state a function's contract are not duplication
  even when they use the same words as the signature; they are the contract.

## Enforcement

Upstream offers no serious enforcement. Its suggestion is to build a program
that interprets colloquial English and decides whether what the comment says
could be better expressed in C++, which is a way of saying the rule is not
checkable today. Treat it as a review rule.

## Related

- [0420](0420-nl-2-state-intent-in-comments.md) — the positive half: what
  comments are for once the redundant ones are gone.
- [0421](0421-nl-3-keep-comments-crisp.md) — the same economy applied to the
  comments that survive.
- [0009](0009-p-3-express-intent.md) — expressing intent in the code itself is
  what makes the comment removable.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — an idea stated in code is
  checked by the compiler; the same idea in a comment is not.
