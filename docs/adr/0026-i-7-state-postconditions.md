# 0026. I.7: State postconditions

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section I (Interfaces), transcribed 2026-08-10

## Context

Stating postconditions detects misunderstandings about the result, and possibly
catches erroneous implementations:

```cpp
int area(int height, int width) { return height * width; }  // bad
```

Here the precondition specification was incautiously left out, so it is not
explicit that `height` and `width` must be positive; and the postcondition was
left out too, so it is not obvious that `height * width` is wrong for areas
larger than the largest integer. Overflow can happen. Consider:

```cpp
int area(int height, int width)
{
    auto res = height * width;
    Ensures(res > 0);
    return res;
}
```

A famous security bug has the same shape:

```cpp
void f()    // problematic
{
    char buffer[MAX];
    // ...
    memset(buffer, 0, sizeof(buffer));
}
```

There was no postcondition stating that the buffer should be cleared, and the
optimiser eliminated the apparently redundant `memset()` call:

```cpp
void f()    // better
{
    char buffer[MAX];
    // ...
    memset(buffer, 0, sizeof(buffer));
    Ensures(buffer[0] == 0);
}
```

Postconditions are often informally stated in a comment describing the purpose
of a function; `Ensures()` makes this more systematic, visible and checkable.
They are especially important when they relate to something not directly
reflected in a returned result, such as the state of a data structure.

Consider a function that manipulates a `Record` using a mutex:

```cpp
mutex m;
void manipulate(Record& r)    // don't
{
    m.lock();
    // ... no m.unlock() ...
}
```

The postcondition that the mutex should be released was never stated, so it is
unclear whether the missing unlock is a bug or a feature. Stating it makes the
bug obvious — but only to a human reading comments:

```cpp
void manipulate(Record& r)    // postcondition: m is unlocked upon exit
```

Better still, use RAII, so the postcondition is enforced in code:

```cpp
void manipulate(Record& r)    // best
{
    lock_guard<mutex> _ {m};
    // ...
}
```

Ideally, postconditions are stated in the interface, so that users can see them.
Only postconditions related to users can be stated there; postconditions related
only to internal state belong in the implementation.

## Decision

State a function's postconditions — what is true of the result and of any state
the function touched when it returns.

- Prefer a postcondition enforced by construction. A `lock_guard` states and
  guarantees "the mutex is unlocked on exit" in a way no comment can.
- Where the postcondition cannot be built in, state it formally as an assertion.
- State user-visible postconditions at the declaration; keep postconditions
  about internal state in the definition.
- State postconditions especially where the effect is not the return value: a
  released resource, a modified argument, an updated data structure.

## Rationale

A postcondition is what makes an implementation checkable against its intent. In
the `area` example, nothing is wrong with the code as written until you know
what it promised; the overflow only becomes a bug once the promise exists.

The `memset` case is the sharpest argument in the section: without a stated and
checked postcondition, a correct-looking call was legally deleted by the
optimiser, and the resulting security bug was invisible in the source.

## Consequences

### Positive

- Wrong implementations are caught against a stated intent rather than against a
  reader's assumption.
- Effects that are not the return value get documented, which is where informal
  documentation is weakest.
- Preferring RAII turns some postconditions into guarantees that cannot be
  violated.

### Negative

- A postcondition can be as wrong as the code, and a checked-but-wrong
  postcondition is worse than none: it fails builds for the wrong reason.
- `Ensures(res > 0)` in the `area` example diagnoses overflow after the
  undefined behaviour has already happened; checking after the fact is
  structurally weaker than not overflowing.
- Writing postconditions on every function is a large amount of text for
  functions whose result is obvious.

### Neutral

- Which facility expresses the assertion is
  [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md).
- Not directly checkable in the general case, though domain-specific checkers
  such as lock-holding checkers exist for many toolchains.

## Enforcement

(Not enforceable.) This is a philosophical guideline that is infeasible to check
directly in the general case. Domain-specific checkers, like lock-holding
checkers, exist for many toolchains.

## Related

- [0024](0024-i-5-state-preconditions-if-any.md) — the other half of the
  contract.
- [0027](0027-i-8-prefer-ensures-for-expressing-postconditions.md) — the form.
- [0014](0014-p-8-do-not-leak-any-resources.md) — RAII as the enforcement
  mechanism for release postconditions.
