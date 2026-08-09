# 0044. F.5: If a function is very small and time-critical, declare it inline

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-10

## Context

No function in this repository is marked `inline` explicitly. Many are inline by
the language's rules — every accessor defined inside a class body, and every
function template — which covers the small accessors in `Panel`, `Result<T>` and
the model types.

## Decision

Adopt F.5 as written. Transcribed in full:

> **Reason.** Some optimizers are good at inlining without hints from the
> programmer, but don't rely on it. Measure! Over the last 40 years or so, we
> have been promised compilers that can inline better than humans without hints
> from humans. We are still waiting. Specifying `inline` (explicitly, or
> implicitly when writing member functions inside a class definition) encourages
> the compiler to do a better job.

**Example.**

```cpp
inline string cat(const string& s, const string& s2) { return s + s2; }
```

**Exception.** Do not put an `inline` function in what is meant to be a stable
interface unless you are certain that it will not change. An `inline` function
is part of the ABI.

**Note.** `constexpr` implies `inline`.

**Note.** Member functions defined in-class are `inline` by default.

**Exception.** Function templates (including member functions of class templates
`A<T>::function()` and member function templates `A::function<T>()`) are
normally defined in headers and therefore `inline`.

**Note.** Consider making functions out of line if they are more than three
statements and can be declared out of line (such as class member functions).

## Rationale

Adopted as written, with emphasis on the word the rule puts an exclamation mark
after: **measure**. This project has no benchmark, so "time-critical" cannot be
established, and therefore the rule's antecedent is almost never satisfied. That
is not a reason to ignore it; it is a reason not to sprinkle `inline`.

The rule's practical content here is its last Note, read in reverse: functions
of more than about three statements should be defined out of line even when they
could be written in the class body. That is a header-hygiene rule as much as a
performance one, and it is the half of F.5 that applies to this codebase daily.

The ABI Exception is inert — see
[0037](0037-i-26-if-you-want-a-cross-compiler-abi-use-a-c-style-subset.md) — so
`inline` carries no compatibility risk here.

## Consequences

### Positive

- Discourages speculative `inline`, which would otherwise be added on intuition
  in a project with no way to check the intuition.
- The out-of-line Note gives a concrete limit on what belongs in a header.

### Negative

- The rule's own advice depends on measurement this project cannot currently
  perform, so in practice it reduces to "don't", which is weaker than what the
  rule intends.
- Small accessors defined in-class are already inline, so the codebase gets the
  benefit without the decision ever being made deliberately — which means it
  would also not be noticed if it stopped being appropriate.

### Neutral

- No deviation is recorded. Zero explicit `inline` is a defensible state under
  this rule, not a violation of it.

## Related

- [0043](0043-f-4-if-a-function-might-have-to-be-evaluated-at-compile-time-declare-it-constexpr.md)
  — the previous rule; `constexpr` implies `inline`.
- [0045](0045-f-6-if-your-function-must-not-throw-declare-it-noexcept.md) — the
  next rule.
- [0015](0015-p-9-do-not-waste-time-or-space.md) — P.9, and the same measurement
  problem.
