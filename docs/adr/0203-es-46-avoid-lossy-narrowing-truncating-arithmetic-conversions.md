# 0203. ES.46: Avoid lossy (narrowing, truncating) arithmetic conversions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

A narrowing conversion destroys information, often unexpectedly so. The key
example is basic narrowing:

```cpp
double d = 7.9;
int i = d;    // bad: narrowing: i becomes 7
i = (int) d;  // bad: we're going to claim this is still not explicit enough

void f(int x, long y, double d)
{
    char c1 = x;   // bad: narrowing
    char c2 = y;   // bad: narrowing
    char c3 = d;   // bad: narrowing
}
```

The guidelines support library offers `narrow_cast` for specifying that
narrowing is acceptable, and `narrow` — "narrow if" — which throws when the
conversion would throw away legal values:

```cpp
i = gsl::narrow_cast<int>(d);   // OK (you asked for it): narrowing: i becomes 7
i = gsl::narrow<int>(d);        // OK: throws narrowing_error
```

Lossy arithmetic casts are included too, such as a negative floating-point value
converted to an unsigned integral type:

```cpp
double d = -7.9;
unsigned u = 0;

u = d;                               // bad: narrowing
u = gsl::narrow_cast<unsigned>(d);   // OK (you asked for it): u becomes 4294967289
u = gsl::narrow<unsigned>(d);        // OK: throws narrowing_error
```

The rule does not apply to contextual conversions to `bool`:

```cpp
if (ptr) do_something(*ptr);   // OK: ptr is used as a condition
bool b = ptr;                  // bad: narrowing
```

## Decision

Never let a conversion lose information implicitly; make every lossy conversion
a visible, deliberate act.

- Do not initialise or assign a narrower arithmetic type from a wider one, and
  do not pass a wider argument to a narrower parameter, without saying so.
- Use brace initialisation, `T{e}`, wherever a conversion is expected to be
  lossless: it is a compile error if it is not. See
  [0214](0214-es-64-use-the-t-e-notation-for-construction.md).
- Where the truncation is intended and the value range is already known good,
  write a checked narrowing helper — `gsl::narrow_cast` upstream — so the intent
  is greppable and the reviewer can see the claim being made.
- Where the value range is not known good, check it and report the failure
  rather than asserting the claim; the upstream `gsl::narrow` does this by
  throwing `narrowing_error`.
- Do not use a C-style cast to perform the narrowing. `(int) d` compiles and
  says nothing about whether the loss was intended.
- Use a pointer or a value directly as a condition rather than converting it to
  `bool` by initialisation.

## Rationale

The point the source makes twice, and that is easy to miss, is that `(int) d` is
not good enough either. Writing a cast proves only that somebody wrote a cast;
it does not distinguish "I know this truncates and I want it to" from "the
compiler complained and I made it stop". A named narrowing operation splits
those two cases, and only one of them needs a reviewer's attention.

The unsigned example is the sharpest argument in the rule. Converting `d` equal
to `-7.9` to `unsigned` does not produce a nearby value: upstream reports
4294967289, and the language does not even promise that much, because the
truncated value `-7` is outside the destination type's range and the conversion
is therefore undefined. Either way an assignment that looks entirely routine has
produced a number unrelated to the one written.

## Consequences

### Positive

- Truncation becomes a written claim rather than an accident, so review has
  something to review.
- Brace initialisation turns the whole class into compile errors wherever the
  conversion was supposed to be lossless.
- The floating-point-to-unsigned case, which is undefined rather than merely
  approximate when the value is out of range, is caught by the same discipline.

### Negative

- Flagging every narrowing conversion produces a great many false positives; the
  upstream Enforcement section says so and offers only heuristics — flag
  float-to-integer, flag `long` to `char`, treat narrowing in function arguments
  as especially suspect — while admitting it lacks the data to be more precise.
- The checked form reports by throwing, so it imposes a failure mechanism on
  every caller and costs a test and a branch at every conversion. Code that can
  afford neither is left with the unchecked form, which is the one that does not
  check.
- The unchecked form is a cast in a nicer coat. `narrow_cast` is defined as a
  `static_cast`, so a wrong claim about the value range fails exactly as
  silently as the implicit conversion it replaced.
- Interfacing with C and platform APIs that use `int`, `long`, `size_t` and
  `DWORD` interchangeably means narrowing at almost every call, so the ceremony
  concentrates at the boundaries where the code is already hardest to read.

### Neutral

- Contextual conversion to `bool` in a condition is explicitly outside this
  rule.
- Widening conversions are unaffected; `double d {some_float}` is the intended
  form for those.

## Enforcement

A good analyser can detect all narrowing conversions, but flagging all of them
leads to a great many false positives. Suggested subsets: flag all
floating-point to integer conversions; flag all `long` to `char` conversions;
and consider narrowing conversions in function arguments especially suspect.

## Related

- [0214](0214-es-64-use-the-t-e-notation-for-construction.md) — `T{e}` is the
  construction syntax that refuses to narrow.
- [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) — the named
  casts, including the narrowing ones.
- [0205](0205-es-48-avoid-casts.md) — why `(int) d` is not an acceptable way to
  say this.
