# 0458. F.25: Use a zstring or a not_null<zstring> to designate a C-style string

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

C-style strings are ubiquitous. They are defined by convention: zero-terminated
arrays of characters. A `char*` in a signature could mean a pointer to a single
character, an old-fashioned pointer to an array of characters, or a
zero-terminated string, and nothing in the type distinguishes them. If null
termination is not needed, `string_view` is the better choice and the question
does not arise.

Consider:

```cpp
int length(const char* p);
```

When `length(s)` is called, should the caller check whether `s` is `nullptr`
first? Should the implementation of `length()` check whether `p` is `nullptr`?
Neither question has an answer, because the signature does not contain one.
Naming the convention answers both:

```cpp
// the implementor of length() must assume that p == nullptr is possible
int length(zstring p);

// it is the caller's job to make sure p != nullptr
int length(not_null<zstring> p);
```

`zstring` does not represent ownership. It says how the characters are
terminated and nothing about who frees them.

## Decision

Say in the type when a parameter is a zero-terminated string, and say in the
type whether it may be null.

- Use `zstring` / `czstring` for a pointer to a zero-terminated, mutable or
  const character sequence.
- Wrap it as `not_null<zstring>` when null is not a valid argument, which moves
  the check to the caller and removes it from the body.
- Prefer `std::string_view` outright wherever null termination is not actually
  required by the code or by an API being called.
- Do not use a bare `char*` or `const char*` in a new interface to mean a
  string; a bare pointer to character means a pointer to one character.
- Do not infer ownership from any of these types. If the callee is to free the
  memory, say so with an owning type, per
  [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md).

## Rationale

The sharpest form of the argument is the pair of questions the plain
`const char*` leaves unanswered: whether the caller checks for null, or the
callee does. When the type does not say, both parties either check — which is
wasted work and dead code on one side — or neither does, which is undefined
behaviour. `zstring` and `not_null<zstring>` distribute that single obligation
explicitly, so exactly one side pays for it.

Recording the type also stops `char*` from silently meaning three different
things across one codebase, which is the ordinary reason string-handling code
is hard to review.

## Consequences

### Positive

- The null contract becomes part of the signature, so it is checkable rather
  than documented.
- A reader can tell a pointer-to-string from a pointer-to-character without
  reading the implementation.
- `not_null` pushes the check to the call site, where the value's provenance is
  usually known and the check often optimises away.
- It nudges code towards `string_view`, since the exercise of naming the
  convention exposes how rarely the termination is genuinely needed.

### Negative

- `zstring`, `czstring`, and `not_null` are Microsoft GSL types, not standard
  ones, so following this rule means taking on a dependency that
  [0019](0019-p-13-use-support-libraries-as-appropriate.md) has to justify, or
  defining local aliases and losing the checking.
- A plain alias for `char*` documents but does not enforce; nothing stops a
  caller passing a non-terminated buffer to a `zstring` parameter.
- `not_null` has a run-time cost in checked builds and changes the type at every
  call site, so retrofitting it to an existing interface is a wide change.
- These types coexist awkwardly with `std::string::c_str()` and with platform
  APIs that use their own char pointer typedefs, so conversions accumulate at
  boundaries.

### Neutral

- The rule concerns what is written in signatures. Inside a function, the string
  is generally converted to `std::string` or `std::string_view` anyway.
- Whether a project takes the GSL types or defines its own aliases is a separate
  decision from adopting the distinction they express.

## Related

- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  general form of the `not_null` half of this rule.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — records
  zero-terminated strings as the named exception to passing arrays as ranges.
- [0457](0457-f-24-use-a-span-t-or-a-span-p-t-to-designate-a-half-open-sequence.md)
  — the range type to reach for when termination is not required.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  principle that a convention should be expressed as a type.
