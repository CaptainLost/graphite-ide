# 0010. P.4: Ideally, a program should be statically type safe

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

An aspiration rather than a gate. Its five problem areas are worth naming here
because this codebase touches three of them: casts (Win32 handles crossing into
`void*` in `DirectoryWatcher`), array decay, and narrowing conversions.

## Decision

Adopt P.4 as an aspiration, not as a blocking rule. Transcribed in full:

> **Reason.** Ideally, a program would be completely statically (compile-time)
> type safe. Unfortunately, that is not possible. Problem areas:
>
> - unions
> - casts
> - array decay
> - range errors
> - narrowing conversions

**Note.** These areas are sources of serious problems (e.g., crashes and
security violations). We try to provide alternative techniques.

**Enforcement.** We can ban, restrain, or detect the individual problem
categories separately, as required and feasible for individual programs. Always
suggest an alternative. For example:

- unions — use `variant` (in C++17)
- casts — minimize their use; templates can help
- array decay — use `span` (from the GSL)
- range errors — use `span`
- narrowing conversions — minimize their use and use `narrow` or `narrow_cast`
  (from the GSL) where they are necessary

## Rationale

Adopted as written, with the GSL substitutions this project actually has:
`std::variant` — already how `Result<T>` is built — and `std::span` in place of
`gsl::span`. There is no substitute for `narrow_cast` here; a checked narrowing
conversion has to be written by hand or not at all.

It is recorded as an aspiration deliberately. A rule that says "ideally" cannot
reject a change on its own, and pretending otherwise would make it the rule
people learn to ignore.

## Consequences

### Positive

- Names the five categories, which is enough to make a cast in review a
  question rather than a detail.
- `std::variant` is already in use for the one place it matters most.

### Negative

- Without `narrow_cast`, narrowing conversions in this codebase are unchecked
  and unmarked, so the rule identifies a hazard it gives no tool against.
- Win32 interop forces casts that no amount of type safety removes; the rule
  offers "minimize", which is not actionable at a call that needs a `HANDLE`.

### Neutral

- Unions: not used anywhere in the repository, so that quarter of the rule is
  inert.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `HANDLE` stored as `void*` and cast at every use | `Core/src/Core/Fs/DirectoryWatcher.cpp` | Deliberate — it keeps `Windows.h` out of the header — but it is a cast per call site |
| Narrowing conversions are unmarked; there is no `narrow_cast` equivalent | throughout `Fs`, `Sln`, `Text` | No inventory taken; would need a compiler warning to find them honestly |

## Related

- [0009](0009-p-3-express-intent.md) — the previous rule.
- [0011](0011-p-5-prefer-compile-time-checking-to-run-time-checking.md) — the next
  rule.
- [0004](0004-return-result-t-instead-of-throwing-across-module-boundaries.md) —
  `std::variant` in use.
