# 0196. ES.34: Don't define a (C-style) variadic function

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

A C-style variadic function is not type safe, and it requires messy
cast-and-macro-laden code to work at all.

```cpp
#include <cstdarg>

// "severity" followed by a zero-terminated list of char*s; write the C-style strings to cerr
void error(int severity ...)
{
    va_list ap;             // a magic type for holding arguments
    va_start(ap, severity); // arg startup: "severity" is the first argument of error()
    for (;;) {
        // treat the next var as a char*; no checking: a cast in disguise
        char* p = va_arg(ap, char*);
        if (!p) break;
        cerr << p << ' ';
    }
    va_end(ap);             // arg cleanup (don't forget this)
    cerr << '\n';
    if (severity) exit(severity);
}

void use()
{
    error(7, "this", "is", "an", "error", nullptr);
    error(7); // crash
    error(7, "this", "is", "an", "error");  // crash
    const char* is = "is";
    string an = "an";
    error(7, "this", "is", an, "error"); // crash
}
```

Only the first call is correct, and the other three all compile. The second and
third omit the terminating `nullptr`, so `va_arg` walks off the end of the
argument list; the fourth passes a `std::string` where a `char*` is read out,
which reinterprets the object's bytes as a pointer. The contract — a severity,
then zero or more `char*`, then a null — exists only in the comment, and nothing
checks any part of it.

The alternative is overloading, templates, and variadic templates:

```cpp
#include <iostream>

void error(int severity)
{
    std::cerr << '\n';
    std::exit(severity);
}

template<typename T, typename... Ts>
constexpr void error(int severity, T head, Ts... tail)
{
    std::cerr << head;
    error(severity, tail...);
}

void use()
{
    error(7); // No crash!
    error(5, "this", "is", "not", "an", "error"); // No crash!
    std::string an = "an";
    error(7, "this", "is", "not", an, "error"); // No crash!
    error(5, "oh", "no", nullptr); // Compile error! No need for nullptr.
}
```

Each argument keeps its type, so `std::string` works without conversion, the
terminator is unnecessary because the pack's length is known, and passing one
anyway is a compile error rather than a crash. Note that this is basically the
way `printf` is implemented — which is why `printf` format-string mismatches
have been a durable source of crashes and vulnerabilities.

## Decision

Do not define a function taking `...`.

- Use a variadic template where the number of arguments varies, so each argument
  keeps its type.
- Use overloads where the argument count varies over a small fixed set.
- Take a `std::initializer_list<T>`, a `std::span<T>`, or a container where the
  arguments are homogeneous.
- Do not include `<cstdarg>` or `<stdarg.h>`, and do not use `va_list`,
  `va_start`, `va_arg` or `va_end` in code defined here.
- Where a C API requires passing through a `va_list` — a logging backend, for
  instance — confine that to the boundary wrapper and expose a typed interface
  inwards, per [0039](0039-i-30-encapsulate-rule-violations.md).
- Do not depend on a terminating sentinel argument to signal the end of a list.

## Rationale

The failure mode is the worst available: every incorrect call in the example
compiles without a diagnostic, and each fails at run time by reading memory that
was never an argument. `va_arg` is a cast with no cast in sight, so the type
system is absent by construction rather than by oversight. A variadic template
gives the same call-site syntax while making all four calls checked, which means
the convenience the C form was chosen for costs nothing to keep.

## Consequences

### Positive

- Argument types are checked, so a mismatch is a compile error rather than
  undefined behaviour.
- The argument count is known to the callee, so sentinel terminators and their
  omission both disappear.
- Class types can be passed directly, without conversion to a C-compatible
  representation.

### Negative

- A variadic template must be defined in a header and is instantiated per
  argument-list shape, which costs compile time and code size where the C form
  cost neither.
- Recursive pack expansion produces long, hard-to-read diagnostics when an
  argument does not support the operation applied to it.
- Interfacing with C libraries that take `...` or a `va_list` remains necessary,
  so the unsafe form persists at the boundary; the rule constrains definitions,
  not calls.
- `std::printf` and friends are variadic, and this rule does not forbid calling
  them, so the format-string hazard is still present in any code that does.

### Neutral

- The rule concerns *defining* a C-style variadic function. Calling an existing
  one is a separate matter.
- The variadic-template replacement is standard C++ and needs no library beyond
  the language itself.

## Enforcement

- Flag definitions of C-style variadic functions.
- Flag `#include <cstdarg>` and `#include <stdarg.h>`.

## Related

- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — a
  `va_arg` is an unchecked cast, which is what that rule is about.
- [0023](0023-i-4-make-interfaces-precisely-and-strongly-typed.md) — the
  interface rule this is a specific violation of.
- [0193](0193-es-31-dont-use-macros-for-constants-or-functions.md) — `va_arg` and
  its companions are macros, with all the attendant problems.
- [0039](0039-i-30-encapsulate-rule-violations.md) — how a C API that requires
  `va_list` is handled.
