# 0436. NL.25: Don't use void as an argument type

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section NL (Naming and layout suggestions), transcribed 2026-08-10

## Context

Writing `void` in an otherwise empty parameter list is verbose, and it is only
needed where compatibility with C matters.

```cpp
void f(void);   // bad
void g();       // better
```

Upstream notes that even Dennis Ritchie deemed `void f(void)` an abomination.
There was an argument for that abomination in C, at a time when function
prototypes were rare: banning

```cpp
int f();
f(1, 2, "weird but valid C89");   // hope that f() is defined
                                  // int f(a, b, c) char* c; { /* ... */ }
```

would have caused major problems. That argument does not apply in the
twenty-first century, and it does not apply in C++, where an empty parameter
list has always meant a function taking no arguments.

The rule sits among suggestions the guidelines offer as defaults, whose stated
aim is consistency rather than any particular convention.

## Decision

Write an empty parameter list as `()`.

- Declare and define parameterless functions as `f()`, not `f(void)`.
- Apply this to member functions, free functions and function pointer types
  alike.
- Keep `(void)` only in a declaration that must also be valid C — a header
  shared with C code, or code deliberately written in the common subset — and
  confine it to that boundary.
- Do not carry `(void)` inwards from such a header into the C++ code that
  implements or calls it.

## Rationale

The `(void)` spelling exists to say something that C++ already says by default,
so it is pure noise once the language is C++: it makes the reader check whether
a distinction is being drawn when none is. Its only remaining purpose is to
mark a declaration as C-compatible, and a spelling that carries no information
except at a boundary is best reserved for that boundary, where it does mean
something.

## Consequences

### Positive

- Parameter lists are shorter and uniform, so an empty one is recognised by its
  shape rather than read.
- `(void)` regains meaning: where it does appear, it signals that the
  declaration is meant to be consumed by a C compiler.
- New readers are not led to look for a distinction between `f()` and `f(void)`
  that does not exist in C++.

### Negative

- It fails exactly where interoperability lives. A header compiled as both C
  and C++ still needs `(void)`, so the codebases with the most function
  declarations crossing a language boundary get the least benefit.
- Upstream gives no enforcement clause, so the rule is applied by habit and
  review only, and a single `(void)` is not worth a review comment on its own.
- Removing `(void)` from existing declarations is churn with no behavioural
  payoff, so a codebase will stay mixed for a long time, which costs more
  consistency than the rule buys.
- The gain is cosmetic. Nothing here catches a bug, and the rule spends
  attention that could be directed at a guideline that does.

### Neutral

- The two spellings declare the same function in C++; this is a question of
  notation, not of meaning.
- Whether a function should take no arguments at all is a separate question
  entirely.

## Related

- [0369](0369-cpl-1-prefer-c-to-c.md) — the general preference that makes the C
  spelling the exception rather than the default.
- [0370](0370-cpl-2-if-you-must-use-c-use-the-common-subset-of-c-and-c-and-compile-the-c-code-as-c.md)
  — the case where `(void)` is still required, and how to contain it.
- [0429](0429-nl-15-use-spaces-sparingly.md) — the same instinct about
  notational noise, applied to whitespace.
- [0432](0432-nl-18-use-c-style-declarator-layout.md) — another place where the
  C inheritance and the C++ reading of a declaration diverge.
