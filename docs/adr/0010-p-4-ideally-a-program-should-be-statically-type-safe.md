# 0010. P.4: Ideally, a program should be statically type safe

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section P (Philosophy), transcribed 2026-08-10

## Context

Ideally, a program would be completely statically — that is, compile-time —
type safe. Unfortunately that is not possible in C++. The problem areas are
known and short:

- unions
- casts
- array decay
- range errors
- narrowing conversions

These areas are sources of serious problems, including crashes and security
violations. Each has an alternative technique that is type safe, or nearly so,
which is what makes the rule actionable rather than aspirational.

## Decision

Treat the five problem areas above as constructs that require a reason, and
prefer the type-safe alternative by default:

| Problem | Use instead |
|---|---|
| unions | `std::variant` |
| casts | minimise; templates can help |
| array decay | `span` |
| range errors | `span` |
| narrowing conversions | minimise; `narrow` or `narrow_cast` where necessary |

Where one of these constructs is genuinely needed, it is confined to the
implementation of an abstraction rather than spread through calling code, and
the reason is stated at the site.

Individual problem categories may be banned, restrained, or merely detected
separately, as required and feasible — but whichever is chosen, an alternative
is always suggested rather than the construct simply forbidden.

## Rationale

This record is the bridge between the philosophy and the concrete rules: it
names, once, the five places where C++'s type system stops holding, so that
later rules can say "use `span`" without re-arguing why.

The upstream framing matters — "ideally" — because full static type safety is
not achievable, and a rule written as an absolute would be broken on day one and
then ignored. Naming the escape hatches and the preferred replacement for each
makes the rule survivable.

## Consequences

### Positive

- The set of constructs that can silently produce a crash or a security bug is
  written down and finite.
- Each entry has a replacement, so applying the rule is a lookup rather than a
  design exercise.
- `variant` and `span` carry their own invariants, so errors that were undefined
  behaviour become diagnosable.

### Negative

- `std::variant` is not a drop-in for a union: it costs a discriminator, and its
  access syntax is heavier than a member access.
- `span` does not eliminate range errors; it makes them checkable. Whether they
  are actually checked depends on the implementation and the build.
- `narrow_cast` documents a narrowing conversion without preventing it, so a
  codebase can be full of them and still satisfy the letter of the rule.
- Removing casts sometimes means introducing templates, which trades one kind of
  complexity for another.

### Neutral

- This record does not decide which of ban / restrain / detect applies to each
  category here; that is per-construct and belongs with the more specific
  records.
- It says nothing about run-time checking, which is
  [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md).

## Enforcement

Ban, restrain, or detect the individual problem categories separately, as
required and feasible for individual programs. Always suggest an alternative:

- unions — use `variant`
- casts — minimise their use; templates can help
- array decay — use `span`
- range errors — use `span`
- narrowing conversions — minimise their use, and use `narrow` or `narrow_cast`
  where they are necessary

## Related

- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — what to
  do with what the type system *can* catch.
- [0012](0012-p-6-what-cannot-be-checked-at-compile-time-should-be-checkable-at-run-time.md)
  — what to do with what it cannot.
- [0007](0007-p-1-express-ideas-directly-in-code.md) — casts neuter the type
  system, which is why they are flagged there too.
