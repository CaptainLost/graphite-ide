# 0031. I.12: Declare a pointer that must not be null as not_null

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

`not_null` is a GSL type, and the GSL is declined by
[0019](0019-p-13-use-support-libraries-as-appropriate.md). This record therefore
adopts the rule's intent and names what expresses it here.

## Decision

Adopt I.12 in substance, using a reference where a pointer must not be null.
Transcribed in full:

> **Reason.** To help avoid dereferencing `nullptr` errors. To improve
> performance by avoiding redundant checks for `nullptr`.

**Example.**

```cpp
int length(const char* p);            // it is not clear whether length(nullptr) is valid

length(nullptr);                      // OK?

int length(not_null<const char*> p);  // better: we can assume that p cannot be nullptr

int length(const char* p);            // we must assume that p can be nullptr
```

By stating the intent in source, implementers and tools can provide better
diagnostics, such as finding some classes of errors through static analysis, and
perform optimizations, such as removing branches and null tests.

**Note.** `not_null` is defined in the guidelines support library.

**Note.** The assumption that the pointer to `char` pointed to a C-style string
(a zero-terminated string of characters) was still implicit, and a potential
source of confusion and errors. Use `czstring` in preference to `const char*`.

```cpp
// we can assume that p cannot be nullptr
// we can assume that p points to a zero-terminated array of characters
int length(not_null<czstring> p);
```

Note: `length()` is, of course, `std::strlen()` in disguise.

**Enforcement.**

- (Simple) ((Foundation)) If a function checks a pointer parameter against
  `nullptr` before access, on all control-flow paths, then warn it should be
  declared `not_null`.
- (Complex) If a function with pointer return value ensures it is not `nullptr`
  on all return paths, then warn the return type should be declared `not_null`.

## Decision detail

Without `not_null`, the rule is expressed as: **if a parameter must not be null,
take a reference.** A pointer parameter means null is a legitimate value, and
the callee is expected to handle it. This gives the same guarantee as
`not_null`, enforced by the language rather than by a wrapper, at the cost of
losing rebindability — which parameters do not need.

Where a pointer genuinely must be nullable and is stored rather than passed, the
nullability stays in the code and is checked at use.

## Rationale

Adopted in substance because the rule's goal is achievable without the GSL, and
the substitute is stronger where it applies: a reference cannot be null at all,
whereas `not_null` is a run-time check in a wrapper.

The substitution is not total, and the gap is worth stating. `not_null` can wrap
a *stored* pointer and a *returned* pointer; a reference member is awkward
(non-assignable) and a returned reference makes lifetime claims that a raw
pointer does not. So the rule is fully met for parameters and only partly met
for members and return values.

`czstring` is unavailable for the same reason. `std::string_view` covers the
case in this codebase, and is used.

## Consequences

### Positive

- A clear reading rule for every signature in Core: pointer means optional,
  reference means required.
- Enforced by the compiler for parameters, with no wrapper type and no run-time
  check.

### Negative

- Members and return values are not covered. `Window*`, `Layer*` and `Panel*`
  stored as members carry their non-null expectation in prose only.
- The rule's optimization argument is lost for the stored cases: the null checks
  that `not_null` would remove are still written by hand where they are written
  at all.

### Neutral

- The first enforcement point — a function that checks against `nullptr` on all
  paths should say so in its type — is a useful review question even without a
  tool to run it.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the previous rule.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the next rule.
- [0019](0019-p-13-use-support-libraries-as-appropriate.md) — why `not_null` and
  `czstring` are unavailable.
