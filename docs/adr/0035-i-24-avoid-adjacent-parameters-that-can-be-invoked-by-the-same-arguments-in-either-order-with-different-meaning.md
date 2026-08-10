# 0035. I.24: Avoid adjacent parameters that can be invoked by the same arguments in either order with different meaning

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Adjacent arguments of the same type are easily swapped by mistake.

```cpp
void copy_n(T* p, T* q, int n);  // copy from [p:p + n) to [q:q + n)
```

This is a nasty variant of a K&R C-style interface: it is easy to reverse the
"to" and "from" arguments, and both orders compile. Using `const` for the "from"
argument distinguishes them:

```cpp
void copy_n(const T* p, T* q, int n);  // copy from [p:p + n) to [q:q + n)
```

If the order of the parameters is not important, there is no problem:

```cpp
int max(int a, int b);
```

Beyond `const`, there are two alternatives. Do not pass arrays as pointers; pass
an object representing a range:

```cpp
void copy_n(span<const T> p, span<T> q);  // copy from p to q
```

Or define a struct as the parameter type and name the fields accordingly:

```cpp
struct SystemParams {
    string config_file;
    string output_path;
    seconds timeout;
};
void initialize(SystemParams p);
```

This tends to make invocations clear to future readers, as the parameters are
often filled in by name at the call site.

Only the interface's designer can adequately address the source of violations of
this guideline.

## Decision

Avoid adjacent parameters that can be invoked by the same arguments in either
order with a different meaning.

- Where two adjacent parameters have the same type but different roles, make the
  types differ. `const` on the input parameter is the cheapest way and is often
  enough.
- Prefer distinct, meaningful types — a range type, a named wrapper — so that
  the wrong order does not compile.
- Where several same-typed values must be supplied, take a struct with named
  fields, so the call site names each value.
- Where the order genuinely does not matter, as in `max(a, b)`, there is nothing
  to fix.

## Rationale

A swapped-argument call is correct C++ that does the wrong thing, which puts it
in the worst category: no diagnostic, no crash, and a result that is plausible
enough to survive a test. Nothing about the call site reveals it, because the
call site is where the error is.

The fix has to be in the interface. A caller cannot protect themselves against
`copy_n(T*, T*, int)`, no matter how careful they are, which is why the rule
puts the responsibility on the designer.

## Consequences

### Positive

- A swapped call becomes a compile error rather than a silent behaviour change.
- `const` on the source parameter costs nothing and documents direction.
- A named-field struct makes the call site self-describing.

### Negative

- Introducing distinct types purely to prevent swapping adds types whose only
  purpose is to be different, which is real weight for a hypothetical mistake.
- A struct parameter moves the naming to the call site but adds a definition,
  and its fields can still be filled in the wrong order when constructed
  positionally.
- The simple enforcement — warn on two consecutive parameters of the same type —
  fires on plenty of interfaces that are perfectly fine.

### Neutral

- Interfaces where order is irrelevant are explicitly out of scope.
- Upstream considers this enforcement strategy provisional; a less simple one is
  still being looked for.

## Enforcement

- (Simple) Warn if two consecutive parameters share the same type.

Upstream notes that a less-simple enforcement is still being sought.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the range fix
  solves both problems in `copy_n`.
- [0034](0034-i-23-keep-the-number-of-function-arguments-low.md) — the
  struct-parameter fix is the same one.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — distinct
  types are the general answer.
