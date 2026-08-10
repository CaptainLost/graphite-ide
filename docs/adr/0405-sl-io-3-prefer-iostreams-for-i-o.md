# 0405. SL.io.3: Prefer iostreams for I/O

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section SL (The Standard Library), transcribed 2026-08-10

## Context

iostreams are safe, flexible, and extensible. They form a type-safe, extensible,
formatted and unformatted I/O library for streaming I/O, supporting multiple and
user-extensible buffering strategies and multiple locales, and usable for
conventional I/O, for reading and writing memory through string streams, and for
user-defined extensions such as streaming across networks.

```cpp
// write a complex number:
complex<double> z{ 3, 4 };
cout << z << '\n';
```

`complex` is a user-defined type, and its I/O is defined without modifying the
iostream library. Reading works the same way, and the stream's own state ends
the loop:

```cpp
// read a file of complex numbers:
for (complex<double> z; cin >> z; )
    v.push_back(z);
```

It is often, and often correctly, pointed out that the `printf()` family has two
advantages over iostreams: flexibility of formatting, and performance. That has
to be weighed against the iostreams advantages of extensibility to handle
user-defined types, resilience against security violations, implicit memory
management, and locale handling. If I/O performance is what you need, you can
almost always do better than `printf()`.

`gets()`, `scanf()` using `%s`, and `printf()` using `%s` are security hazards,
vulnerable to buffer overflow and generally error-prone. C11 defines some
optional extensions that do extra checking of their arguments; if present in
your C library, `gets_s()`, `scanf_s()` and `printf_s()` might be safer
alternatives, but they are still not type safe.

## Decision

Do formatted and unformatted I/O through streams rather than through the
`printf` family.

- Write with `operator<<` and read with `operator>>` or `getline`.
- Give a user-defined type its own stream operators rather than a format string
  and a list of members.
- Do not use `gets()`, `scanf("%s", ...)` or `printf("%s", ...)`; the
  `_s`-suffixed variants are an improvement but are not type safe and are not a
  substitute.
- Use string streams for formatting into memory, rather than `sprintf` into a
  character buffer.
- Do not choose the `printf` family for performance without a measurement, and
  do not assume it is the fastest option available.

## Rationale

The decisive difference is that the format string is a second, unchecked
description of the argument list. `printf` cannot know what it was passed, so a
mismatch between the format and the arguments is undefined behaviour that the
language has no means to detect — the compiler warnings that catch it are an
extra-linguistic favour, and they stop at the first non-literal format string.
The stream operator takes the argument's type as the whole of its interface, so
the mismatch cannot be expressed.

Extensibility follows from the same property. `cout << z` works for a type the
iostream library has never heard of, without touching that library, because
overload resolution is the dispatch mechanism.

## Consequences

### Positive

- Formatting is type-checked by overload resolution rather than by a separate
  format string.
- User-defined types get first-class I/O without modifying the library.
- Output buffers are managed by the stream, so the overflow class of defect
  disappears.
- Locale handling and buffering strategies are available without changing call
  sites.

### Negative

- Controlling layout through manipulators is genuinely worse than a format
  string: the state is sticky, it is set by separate statements, and the result
  is longer and harder to read than the equivalent `printf`.
- Streams carry per-call formatting state and virtual dispatch, so the
  performance objection is real even if the conclusion usually is not.
- Errors are reported through stream state rather than through a return value,
  which is easy to ignore entirely.
- Upstream notes an exception on performance grounds but leaves it unwritten, so
  the rule's own escape hatch has no stated criterion.
- The choice is no longer binary: `std::format` provides checked, format-string
  style output that is neither iostreams nor `printf`, and this rule predates
  that option.

### Neutral

- The rule is about which library performs I/O, not about how much is read at a
  time.
- Nothing here prevents calling a C API that requires `FILE*`; the rule concerns
  code you write.

## Enforcement

- Optionally flag `<cstdio>` and `<stdio.h>`.

## Related

- [0403](0403-sl-io-1-use-character-level-input-only-when-you-have-to.md) — the
  granularity rule, which assumes stream extraction.
- [0407](0407-sl-io-50-avoid-endl.md) — the one place where the stream idiom
  costs performance for no benefit.
- [0196](0196-es-34-dont-define-a-c-style-variadic-function.md) — why the
  `printf` family's interface is unchecked in the first place.
- [0243](0243-per-6-dont-make-claims-about-performance-without-measurements.md)
  — required before invoking the performance argument in either direction.
