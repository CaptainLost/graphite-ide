# 0455. F.22: Use T* or owner<T*> to designate a single object

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

The reason is readability: it makes the meaning of a plain pointer clear, and it
enables significant tool support.

In traditional C and C++ code, a plain `T*` is used for many weakly related
purposes. It may identify a single object that this function is not to delete;
it may point to an object allocated on the free store, to be deleted later; it
may hold `nullptr`; it may identify a C-style, zero-terminated string; it may
identify an array whose length is specified separately; or it may identify a
location within an array. This makes it hard to understand what the code does
and is supposed to do, and it complicates both checking and tool support.

```cpp
void use(int* p, int n, char* s, int* q)
{
    p[n - 1] = 666; // Bad: we don't know if p points to n elements;
                    // assume it does not or use span<int>
    cout << s;      // Bad: we don't know if that s points to a zero-terminated array of char;
                    // assume it does not or use zstring
    delete q;       // Bad: we don't know if *q is allocated on the free store;
                    // assume it does not or use owner
}
```

Better:

```cpp
void use2(span<int> p, zstring s, owner<int*> q)
{
    p[p.size() - 1] = 666; // OK, a range error can be caught
    cout << s; // OK
    delete q;  // OK
}
```

`owner<T*>` represents ownership and `zstring` represents a C-style string. A
`T*` obtained from a smart pointer to `T`, such as a `unique_ptr<T>`, should
likewise be assumed to point to a single element.

## Decision

Let a plain `T*` mean one thing — a pointer to a single object this function
does not own — and use a named type for every other meaning.

- Write `T*` only where the target is one object whose lifetime belongs to
  somebody else.
- Write `owner<T*>` where a raw pointer must carry ownership, so that the
  `delete` is legitimate and tool-visible.
- Write `span<T>` for a sequence, `zstring` or `czstring` for a zero-terminated
  string, and never a bare `T*` with a separate length.
- Do no pointer arithmetic on a `T*`: if indexing or advancing is wanted, the
  parameter was a range and should have been a `span`.
- Treat a `T*` obtained from `unique_ptr::get()` or the equivalent as a
  single-object, non-owning pointer, which is what this rule already says it is.

## Rationale

The value of the rule is not in what `T*` gains but in what it gives up. Once
the plain pointer means only "one object, not mine", every other meaning is
forced to announce itself in a type, and a checker can then act on `p[n - 1]` or
`delete q` without knowing anything about the program: the first is arithmetic
on a single-object pointer and the second is a delete of something not marked as
owned. That is precisely the tool support the upstream reason mentions, and it
is unavailable as long as one spelling covers six meanings.

## Consequences

### Positive

- Ownership, extent, and string-ness each become visible in the signature
  instead of in a comment or a convention.
- Bounds and ownership checkers gain something to check, because the exceptions
  are marked rather than assumed.
- The reduced meaning of `T*` makes the common case — a borrowed reference to
  one object — instantly recognisable.
- `owner<T*>` documents a raw owning pointer at the boundaries where one is
  unavoidable.

### Negative

- `owner<T*>`, `zstring` and `span` come from the guidelines support library, so
  the rule presupposes a dependency; without it the vocabulary must be defined
  locally and the tool support does not follow.
- `owner<T*>` is an alias, not a type: it changes nothing at compile time and
  nothing at run time, so a mislabelled or unlabelled pointer is caught only by
  a checker that may not be run.
- Converting an existing interface means changing types at every call site, and
  a `T*` that crosses a C ABI cannot be converted at all.
- Banning pointer arithmetic on `T*` conflicts with idiomatic iterator-style
  code and with any interface that returns a position within an array.

### Neutral

- The rule assigns meanings to spellings; it does not say which pointers a
  design ought to have.
- A pointer obtained from a smart pointer needs no annotation, since the
  single-object reading is already the default.

## Enforcement

- (Simple) Warn for any arithmetic operation on an expression of pointer type
  that results in a value of pointer type.

## Related

- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — the sequence
  case, which this rule refers to and shares an enforcement bullet with.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  where `owner<T*>` is permitted and what it means.
- [0456](0456-f-23-use-a-not-null-t-to-indicate-that-null-is-not-a-valid-value.md)
  — the nullability axis, which this rule leaves open.
- [0454](0454-f-60-prefer-t-over-t-when-no-argument-is-a-valid-option.md) — when
  a single-object pointer should have been a reference instead.
