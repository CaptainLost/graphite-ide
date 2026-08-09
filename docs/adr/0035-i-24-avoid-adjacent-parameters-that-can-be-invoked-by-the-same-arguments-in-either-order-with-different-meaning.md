# 0035. I.24: Avoid adjacent parameters that can be invoked by the same arguments in either order with different meaning

Status: accepted
Date: 2026-08-09
Source: C++ Core Guidelines, release 0.8, Jun 14, 2026, transcribed 2026-08-09

## Context

This codebase has one clear instance:
`DocumentSet::Add(const std::filesystem::path& path, bool permanent, bool confirmed)`
in `Core/src/Core/Text/DocumentSet.h:156`. Two adjacent `bool`s, swappable at
every call site, with entirely different meanings and no compiler diagnostic if
they are reversed.

## Decision

Adopt I.24 as written. Transcribed in full:

> **Reason.** Adjacent arguments of the same type are easily swapped by mistake.

**Example, bad.** Consider:

```cpp
void copy_n(T* p, T* q, int n);  // copy from [p:p + n) to [q:q + n)
```

This is a nasty variant of a K&R C-style interface. It is easy to reverse the
"to" and "from" arguments.

Use `const` for the "from" argument:

```cpp
void copy_n(const T* p, T* q, int n);  // copy from [p:p + n) to [q:q + n)
```

**Exception.** If the order of the parameters is not important, there is no
problem:

```cpp
int max(int a, int b);
```

**Alternative.** Don't pass arrays as pointers, pass an object representing a
range (e.g., a span):

```cpp
void copy_n(span<const T> p, span<T> q);  // copy from p to q
```

**Alternative.** Define a struct as the parameter type and name the fields for
those parameters accordingly:

```cpp
struct SystemParams {
    string config_file;
    string output_path;
    seconds timeout;
};
void initialize(SystemParams p);
```

This tends to make invocations of this clear to future readers, as the
parameters are often filled in by name at the call site.

**Note.** Only the interface's designer can adequately address the source of
violations of this guideline.

**Enforcement strategy.** (Simple) Warn if two consecutive parameters share the
same type. We are still looking for a less-simple enforcement.

## Rationale

Adopted as written. Its enforcement strategy — warn on two consecutive
parameters of the same type — is mechanical and finds exactly one thing here,
which is the ideal ratio for a rule: cheap to check, and what it finds is real.

`Add(path, permanent, confirmed)` is the case the rule describes precisely. A
call reading `Add(p, true, false)` conveys nothing at the call site, and
reversing the two flags compiles and changes behaviour silently. The rule's
second Alternative — a named struct — is the fix, or a small enum per flag
(`Permanence::Permanent`, `Confirmation::Confirmed`), which additionally kills
the bare `true`/`false` that
[0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) objects to.

The `const`-on-the-source trick from the first example is worth remembering for
any future copy-shaped function, though none exists here today.

## Consequences

### Positive

- One mechanically checkable rule, one real finding, one obvious fix.
- Fixing it also satisfies I.4's "no more than one `bool` parameter" — the two
  rules point at the same signature from different angles.

### Negative

- The fix costs a type per flag or a struct, and touches every call site of
  `Add`. For a signature that has not yet been miscalled, that is speculative
  work, and it will compete with features.
- The rule cannot see semantic swappability across differing types — two
  `std::filesystem::path` parameters meaning source and destination would be
  just as swappable and would not trip the same-type check if one were
  `path` and the other `string`.

### Neutral

- The Exception applies to nothing here; there is no commutative two-argument
  function in the codebase where the order genuinely does not matter.

## Deviations to fix

| What | Where | Note |
|---|---|---|
| `Add(const path&, bool permanent, bool confirmed)` — two adjacent, swappable `bool`s | `Core/src/Core/Text/DocumentSet.h:156` | The only instance in the repository. Two small enums are the smaller fix; a parameter struct the larger one |

## Related

- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the previous
  rule.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the next rule.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the same
  signature, from the strong-typing angle.
