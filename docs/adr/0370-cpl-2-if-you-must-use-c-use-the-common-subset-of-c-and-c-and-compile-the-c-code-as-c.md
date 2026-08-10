# 0370. CPL.2: If you must use C, use the common subset of C and C++, and compile the C code as C++

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section CPL (C-style programming), transcribed 2026-08-10

## Context

Neither C nor C++ is a subset of the other, but they share a large common
subset. Code written inside that subset can be compiled by both a C compiler
and a C++ compiler, and when compiled as C++ it is better type checked than
"pure C" would be — the same text gets stricter conversion rules, stricter
`const`, and no implicit function declarations.

Allocation shows where the subset lies:

```cpp
int* p1 = malloc(10 * sizeof(int));                      // not C++
int* p2 = static_cast<int*>(malloc(10 * sizeof(int)));   // not C, C-style C++
int* p3 = new int[10];                                   // not C
int* p4 = (int*) malloc(10 * sizeof(int));               // both C and C++
```

`p1` relies on the implicit `void*` conversion that only C has. `p2` uses a
named cast that only C++ has. `p3` uses `new`, which C does not have at all.
Only `p4`, with the C-style cast written out, is accepted by both languages —
so the common subset is narrower than either language, and staying inside it
means giving up the better spelling on the C++ side.

## Decision

Where code must remain compilable as C, keep it inside the common subset, and
still build it as C++.

- Do not introduce a build configuration that compiles a translation unit as C.
  Every source file goes through the C++ compiler.
- In shared-subset code, write the conversions out explicitly rather than
  relying on C's implicit conversion from `void*`, since C++ will reject the
  implicit form.
- Accept the C-style cast in that code as the price of dual compilability; it is
  the one place where [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md)
  is deliberately not followed, and it is confined to the shared subset.
- Do not enable compiler extension options that would weaken the C++ compiler's
  checking in order to accept C-only constructs.
- Keep the shared-subset region small and named, so that the constructs it
  contains are not mistaken for the house style.

## Rationale

Compiling as C++ is free: the code is unchanged, but a whole category of
error — implicit `void*` conversions, missing prototypes, weaker `const` — is
diagnosed rather than accepted. That is the sharpest point of the rule, and it
applies even to code that is otherwise entirely C in character.

The subset is not a comfortable place to live. `p4` is the only line of the four
that works everywhere, and it is the worst of them. That is an argument for
making the shared region as small as it can be, not for abandoning the practice
where dual compilation is a genuine requirement.

## Consequences

### Positive

- Code stays checkable by the stricter of the two compilers without any change
  to what it says.
- The dual-compilable region is explicit and bounded rather than spread through
  the codebase by habit.
- Errors that C accepts silently — an undeclared function, a `void*` assigned
  to a typed pointer — become build failures.

### Negative

- The subset costs expressiveness in both directions: no `new`, no named casts,
  no references, no templates, no RAII. Code in it is worse C++ than it needed
  to be.
- It puts this record in direct conflict with
  [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) and
  [0153](0153-r-10-avoid-malloc-and-free.md); the conflict is real, and readers
  must know the subset region is the exception.
- Nothing in the build detects drift. Once code is only ever compiled as C++,
  C++-only constructs creep in and the dual compilability quietly stops being
  true.
- The rule offers no help in deciding when C is genuinely required, which is the
  judgement that actually matters.

### Neutral

- The rule governs code that must remain dual-compilable. Third-party C sources
  built as they ship are not code it reaches.
- Compiling as C++ does not make the code C++. It makes it better-checked C.

## Enforcement

- Flag any build mode that compiles code as C.
- The C++ compiler will then enforce that the code is valid C++, provided C
  extension options are not used.

## Related

- [0369](0369-cpl-1-prefer-c-to-c.md) — the first choice, of which this is the
  fallback.
- [0371](0371-cpl-3-if-you-must-use-c-for-interfaces-use-c-in-the-calling-code-using-such-interfaces.md)
  — the interface case, where the subset is used deliberately.
- [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) — the rule the
  common subset forces this record to break.
- [0039](0039-i-30-encapsulate-rule-violations.md) — how a bounded region of
  deliberately non-conforming code is meant to be handled.
