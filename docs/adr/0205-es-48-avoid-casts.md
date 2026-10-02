# 0205. ES.48: Avoid casts

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Casts are a well-known source of errors, and they make some optimisations
unreliable. The upstream example is worth reading slowly:

```cpp
double d = 2;
auto p = (long*)&d;
auto q = (long long*)&d;
cout << d << ' ' << *p << ' ' << *q << '\n';
```

What would you think this fragment prints? The result is at best implementation
defined. The author got:

```
2 0 4611686018427387904
```

Adding a write through one of those pointers:

```cpp
*q = 666;
cout << d << ' ' << *p << ' ' << *q << '\n';
```

produced:

```
3.29048e-321 666 666
```

Surprised? The author was just glad the program did not crash.

Programmers who write casts typically assume that they know what they are doing,
or that writing a cast makes the program easier to read. In fact they often
disable the general rules for using values. Overload resolution and template
instantiation usually pick the right function if there is a right function to
pick; if there is not, perhaps there ought to be, rather than a local fix
applied with a cast.

Casts are necessary in a systems programming language — how else would the
address of a device register get into a pointer? — but they are seriously
overused as well as a major source of errors. If you feel the need for a lot of
casts, there might be a fundamental design problem. The type profile bans
`reinterpret_cast` and C-style casts outright.

Never cast to `(void)` to ignore a `[[nodiscard]]` return value. If you
deliberately want to discard such a result, first think hard about whether that
is really a good idea; there is usually a good reason the author of the function
or of the return type marked it. If you still think it is appropriate and your
reviewer agrees, use `std::ignore =`, which is simple, portable and easy to
grep.

Modern C++ has rules and constructs that remove the need for casts in many
contexts: use templates, use `std::variant`, rely on the well-defined and safe
implicit conversions between pointer types, and use `std::ignore =` for ignored
`[[nodiscard]]` values.

## Decision

Treat a cast as an admission that the type system has been overruled, and look
for the design that does not need one.

- Do not write a C-style cast, `(T)e`, anywhere. Do not write a functional-style
  cast, `T(e)`; use `T{e}` per
  [0214](0214-es-64-use-the-t-e-notation-for-construction.md).
- Before writing any cast, check whether an overload, a template, a concept, a
  `std::variant`, or a fixed interface would remove the need for it.
- Do not cast between unrelated pointer types to reinterpret an object's
  representation. Where a bit pattern really must be reinterpreted, use
  `std::bit_cast`, which C++20 provides, or `std::memcpy`, rather than a
  pointer cast that also violates the aliasing rules.
- Never cast to `(void)` to silence a `[[nodiscard]]` warning. Use
  `std::ignore =`, and only after deciding that discarding the value is
  genuinely correct — which, where the attribute is there because ignoring the
  value drops an error, it almost never is.
- Where a cast is unavoidable, confine it to as small a scope as possible and
  use a named cast; see
  [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md).
- If a change requires many casts, stop and reconsider the design rather than
  writing them.

## Rationale

The example is doing more than showing an implementation-defined result. Writing
through `q` changed `d` to `3.29048e-321` — a value the program's author never
computed and could not predict — because the cast told the compiler to stop
believing the declared types, and the optimiser was still entitled to believe
them. A cast does not merely convert; it withdraws the compiler's ability to
help.

The observation that overload resolution "usually picks the right function if
there is a right function to pick" is the constructive half of the rule. A cast
at a call site is very often the symptom of a missing overload, a missing
conversion, or an interface that is typed too loosely — and fixing that helps
every other caller too.

## Consequences

### Positive

- Removing casts restores the compiler's ability to diagnose type errors, which
  is the point of having types at all.
- Cast-free code stays correct through refactoring; a cast is exactly the
  construct that keeps compiling after the types it was written against change.
- Pushing back on casts surfaces missing overloads and under-specified
  interfaces, which are the real defects.

### Negative

- Casts really are necessary in a systems language, so this is a rule with a
  permanent exception list, and every exception is a judgement about whether
  someone tried hard enough to avoid it.
- The alternatives are not free: introducing a template or a `std::variant` to
  avoid one cast can cost compile time, code size and readability far in excess
  of the cast.
- Interfacing with C APIs — a `void*` user-data pointer handed back to a
  callback is the canonical case — cannot be done without a cast, and no amount
  of local redesign changes that.
- `std::ignore =` is a better spelling than `(void)` but it still discards the
  value; the rule replaces an ugly discard with a tidy one and relies on
  reviewer judgement for the part that matters.

### Neutral

- Removing a cast sometimes changes which overload is selected. That is the
  intended effect, but it means cast removal is a behavioural change and needs
  the same care as any other.
- Brace initialisation is the recommended replacement for the lossless
  conversions, and is not considered a cast.

## Enforcement

- Flag all C-style casts, including casts to `void`.
- Flag functional-style casts using `Type(value)`; `Type{value}` is not
  narrowing and should be used instead.
- Flag identity casts between pointer types where the source and target types
  are the same.
- Flag an explicit pointer cast that could have been implicit.

## Related

- [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) — what to write
  when the cast really is unavoidable.
- [0207](0207-es-50-dont-cast-away-const.md) — the specific cast with the
  worst consequences.
- [0214](0214-es-64-use-the-t-e-notation-for-construction.md) — the construction
  notation that replaces the functional-style cast.
