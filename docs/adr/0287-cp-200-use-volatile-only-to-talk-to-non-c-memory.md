# 0287. CP.200: Use volatile only to talk to non-C++ memory

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section CP (Concurrency and parallelism), transcribed 2026-08-10

## Context

`volatile` is used to refer to objects that are shared with "non-C++" code, or
with hardware that does not follow the C++ memory model.

```cpp
const volatile long clock;
```

This describes a register constantly updated by a clock circuit. `clock` is
`volatile` because its value will change without any action from the C++ program
that uses it: reading `clock` twice will often yield two different values, so
the optimiser had better not optimise away the second read in this code:

```cpp
long t1 = clock;
// ... no use of clock here ...
long t2 = clock;
```

`clock` is `const` because the program should not try to write to it.

Unless you are writing the lowest level code manipulating hardware directly,
consider `volatile` an esoteric feature that is best avoided.

Usually C++ code receives volatile memory that is owned elsewhere, by hardware
or by another language:

```cpp
int volatile* vi = get_hardware_memory_location();
    // note: we get a pointer to someone else's memory here
    // volatile says "treat this with extra respect"
```

Sometimes C++ code allocates the volatile memory and shares it with "elsewhere"
by deliberately escaping a pointer:

```cpp
static volatile long vl;
please_use_this(&vl);   // escape a reference to this to "elsewhere" (not C++)
```

Volatile local variables are nearly always wrong — how can they be shared with
other languages or hardware if they are ephemeral? The same applies almost as
strongly to member variables, for the same reason:

```cpp
// bad
void f()
{
    volatile int i = 0; // bad, volatile local variable
    // etc.
}

class My_type {
    volatile int i = 0; // suspicious, volatile member variable
    // etc.
};
```

In C++, unlike in some other languages, `volatile` has nothing to do with
synchronisation.

## Decision

Qualify an object `volatile` only when something outside the C++ program writes
or reads it.

- Use it for memory-mapped hardware registers and for memory deliberately shared
  with code in another language.
- Add `const` as well where the program must not write, as in the clock
  register.
- Do not use `volatile` to communicate between threads. That is what `atomic<T>`
  is for, and `volatile` gives none of the guarantees being assumed.
- Do not declare a `volatile` local variable, and treat a `volatile` member
  variable as a mistake until shown otherwise; neither can be the memory the
  other side is looking at.
- Where volatile memory is allocated in C++ and its address handed out, keep
  that escape in one place and name what is on the other end.
- Keep the volatile accesses behind an ordinary interface so the rest of the
  program does not carry the qualifier around.

## Rationale

`volatile` is a statement about the compiler's freedom to elide or reorder an
access to a particular object, not about what other threads observe — the two
are constantly confused, and the confusion produces code that looks synchronised
and is not. The locality test in the rule is the useful one: if the object
cannot possibly be seen by hardware or by another language, then there is no
party for `volatile` to be talking to, and the qualifier is either noise or a
mistaken attempt at synchronisation.

## Consequences

### Positive

- The one legitimate use — hardware and foreign memory — is stated precisely
  enough to check against a declaration.
- The commonest misuse, `volatile` as a cheap `atomic`, is ruled out explicitly,
  including in the double-checked locking pattern.
- The local-and-member test is mechanical, so a tool can apply it.
- Confining `volatile` to a boundary keeps the qualifier from spreading through
  the types of everything that touches such a value.

### Negative

- The rule says clearly what `volatile` is not for, but the case it permits is
  where the language's guarantees are thinnest: what an access actually compiles
  to is a property of the platform, which
  [0283](0283-cp-101-distrust-your-hardware-compiler-combination.md) tells you
  to distrust.
- `volatile` suppresses optimisation wherever it appears, so a qualifier applied
  more broadly than necessary costs performance silently and nothing reports it.
- The enforcement heuristic catches `volatile` locals and members, but not a
  pointer-to-volatile passed around freely, which is the form the legitimate use
  takes and also the form a misuse can hide in.
- Escaping the address of a `static volatile` to non-C++ code deliberately steps
  outside what the type system can reason about; the rule sanctions it without
  giving any way to describe the contract on the other side.

### Neutral

- This is a rule about a keyword's meaning, not about concurrency design; it
  appears in the concurrency section because of what it is mistaken for.
- Most code will never need `volatile` at all, which is the intended outcome.

## Enforcement

- Flag `volatile T` local and member variables; almost certainly `atomic<T>` was
  intended instead.

## Related

- [0286](0286-cp-111-use-a-conventional-pattern-if-you-really-need-double-checked-locking.md)
  — the bad example there is precisely `volatile` used as if it synchronised.
- [0282](0282-cp-100-dont-use-lock-free-programming-unless-you-absolutely-have-to.md)
  — atomics under the default ordering are the tool people reach for `volatile`
  to imitate.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — hardware access is the kind of construct to put behind an interface.
- [0008](0008-p-2-write-in-iso-standard-c.md) — talking to non-C++ memory is
  where a program leaves the standard's guarantees behind.
