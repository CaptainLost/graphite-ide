# 0138. C.183: Don't use a union for type punning

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

It is undefined behaviour to read a union member with a different type from the
one with which it was written. Such punning is invisible, or at least harder to
spot than using a named cast, and type punning through a union is a source of
errors.

```cpp
union Pun {
    int x;
    unsigned char c[sizeof(int)];
};
```

The idea of `Pun` is to be able to look at the character representation of an
`int`.

```cpp
void bad(Pun& u)
{
    u.x = 'x';
    cout << u.c[0] << '\n';     // undefined behavior
}
```

If you want to see the bytes of an `int`, use a named cast:

```cpp
void if_you_must_pun(int& x)
{
    auto p = reinterpret_cast<std::byte*>(&x);
    cout << p[0] << '\n';     // OK; better
    // ...
}
```

Accessing the result of a `reinterpret_cast` from the object's declared type to
`char*`, `unsigned char*`, or `std::byte*` is defined behaviour. Using
`reinterpret_cast` is discouraged, but at least it is visible that something
tricky is going on.

Unfortunately, unions are commonly used for type punning. "Sometimes, it works
as expected" is not a conclusive argument. C++17 introduced the distinct type
`std::byte` to facilitate operations on raw object representation; use that type
instead of `unsigned char` or `char` for these operations.

## Decision

Do not read a union member other than the one last written.

- Do not declare a union whose purpose is to view one type's bytes as another.
- Where the object representation must be inspected, `reinterpret_cast` the
  object's address to `std::byte*` and read through that.
- Use `std::byte` rather than `char` or `unsigned char` for raw representation,
  so the intent is not confused with text.
- Where one type's value must be reproduced as another's — reading a float from
  a wire format, for instance — copy the bytes with `std::memcpy` into an object
  of the destination type rather than aliasing storage.
- Confine any such conversion to a small named function, so that the rest of the
  code deals in values rather than representations.

## Rationale

The union form and the cast form are equally unsafe in intent and unequal in
everything else: the cast is defined behaviour for the byte types, and it is
visible. A union used for punning looks like ordinary member access, so nothing
in the source marks the moment the type system was set aside, and the code
frequently produces the expected answer on the compiler where it was written —
which is what allows it to survive until an optimiser that trusts the aliasing
rules changes the result.

## Consequences

### Positive

- The one operation that is actually defined — reading an object's bytes through
  a byte pointer — replaces one that never was.
- Type punning becomes conspicuous at the point it happens, so it can be
  reviewed and confined.
- `std::byte` distinguishes raw representation from character data, which
  `unsigned char` never did.

### Negative

- The rule forbids a widespread and long-established idiom that appears
  throughout existing C and C++ code, so it constrains new code while old code
  and third-party headers continue to rely on it.
- C permits union punning, so shared headers and C interop code cannot follow
  this rule and remain compatible.
- The `memcpy` alternative depends on the compiler eliding the copy to be
  competitive, which is normally the case but is not guaranteed, so there is a
  real if usually theoretical cost.
- Nothing diagnoses the violation: the offending code compiles, runs, and often
  produces the intended answer, which is the worst possible feedback.

### Neutral

- C++20 provides `std::bit_cast` for value-preserving reinterpretation between
  trivially copyable types of the same size; it is a better answer than either
  form above wherever it applies.
- Upstream offers no enforcement for this rule.

## Related

- [0136](0136-c-181-avoid-naked-unions.md) — reading the wrong member by
  accident; this record is about doing it on purpose.
- [0135](0135-c-180-use-unions-to-save-memory.md) — the use a union is
  legitimately for.
- [0010](0010-p-4-ideally-a-program-should-be-statically-type-safe.md) — punning
  is a deliberate breach, and a visible one is preferable to a hidden one.
- [0039](0039-i-30-encapsulate-rule-violations.md) — where representation must
  be inspected, it belongs behind one named function.
