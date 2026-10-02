# 0148. R.2: In interfaces, use raw pointers to denote individual objects (only)

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Arrays are best represented by a container type, such as an owning `vector`, or
by a non-owning `span`. Containers and views hold enough information to do range
checking; a pointer does not.

```cpp
void f(int* p, int n)   // n is the number of elements in p[]
{
    // ...
    p[2] = 7;   // bad: subscript raw pointer
    // ...
}
```

The compiler does not read comments, and without reading other code you do not
know whether `p` really points to `n` elements. Use a `span` instead.

A raw pointer is fine when it denotes one object:

```cpp
void g(int* p, int fmt)   // print *p using format #fmt
{
    // ... uses *p and p[0] only ...
}
```

The exception is C-style strings, which are passed as single pointers to a
zero-terminated sequence of characters. Use `zstring` rather than `char*` to
indicate that you are relying on that convention.

Many current uses of pointers to a single element could be references instead.
Where `nullptr` is a possible value, however, a reference is not a reasonable
alternative.

## Decision

In an interface, a raw pointer denotes a single object and nothing else.

- Do not subscript a raw pointer parameter and do not do arithmetic on one.
- Where a sequence is meant, take a `std::span`, a `std::string_view`, or a
  reference to a container, so that the extent travels with the data.
- Do not take a `(pointer, count)` pair as a substitute; the count is a second
  source of truth that nothing relates to the first.
- Where the single object is always present, prefer a reference; keep the
  pointer when `nullptr` is a legitimate argument, and say so.
- Where a zero-terminated C string is meant, name it `zstring` or `czstring` so
  that the convention is stated rather than assumed.

## Rationale

This rule is what gives a raw pointer a single meaning in a signature. Once `T*`
means "one `T`, possibly none", every reader of every function knows what an
unadorned pointer parameter promises, and the presence of anything else — a
`span`, a smart pointer, a reference — is itself information. The alternative is
that each `T*` parameter carries a convention held in a comment, and comments do
not constrain callers.

## Consequences

### Positive

- Range checking becomes possible at all, because the callee knows the extent.
- Subscripting and pointer arithmetic disappear from interfaces, taking a large
  class of out-of-bounds bug with them.
- The distinction between "one object" and "a sequence" is visible in the type
  rather than in a parameter name.

### Negative

- The pointer-arithmetic check generates a huge number of false positives on an
  older code base, so the rule is far cheaper to adopt at the start than to
  retrofit.
- A `span` does not survive a C ABI boundary, so `(pointer, count)` persists at
  exactly the interfaces where errors are most expensive.
- The zero-terminated string exception reintroduces the very thing the rule
  forbids: a raw pointer that is really a sequence, whose extent is discovered
  by scanning.
- Replacing a pointer with a reference removes the ability to say "absent", so
  applying the note mechanically loses information some callers need.

### Neutral

- Whether the container is owning or a view is a separate question, decided by
  who owns the data.
- This is a rule about interfaces; an implementation may still walk memory with
  a pointer inside a resource handle.

## Enforcement

- Flag pointer arithmetic, including `++`, on a pointer that is not part of a
  container, a view, or an iterator. Applied to an older code base this rule
  would generate a very large number of false positives.
- Flag array names passed as simple pointers.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the interface
  rule this restates from the resource-management side.
- [0149](0149-r-3-a-raw-pointer-a-t-is-non-owning.md) — the other thing a raw
  pointer parameter promises.
- [0157](0157-r-14-avoid-parameters-prefer-span.md) — the same point about array
  parameter syntax.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — how
  to say that the single object is always there.
