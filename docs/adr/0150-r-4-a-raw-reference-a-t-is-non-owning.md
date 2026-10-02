# 0150. R.4: A raw reference (a T&) is non-owning

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

Nothing in the C++ standard, and nothing in most code, says that a `T&` owns
what it refers to, and most raw references do not. Owners need to be
identifiable so that the objects they own can be deleted reliably and
efficiently.

```cpp
void f()
{
    int& r = *new int{7};  // bad: raw owning reference
    // ...
    delete &r;             // bad: violated the rule against deleting raw
                           // pointers
}
```

The example is short because there is very little to say that the raw pointer
rule does not already say. A reference obtained by dereferencing a fresh
allocation is an owner in disguise, and the only way to release it is to take
its address and delete that, which is itself forbidden.

An owning reference is in one respect worse than an owning pointer: there is no
`owner<T&>` to annotate it with, so the escape hatch that legacy pointer code
has does not exist. There is also no way to say that a reference has been
released, since a reference cannot be reseated or set to null, so a released
owning reference is indistinguishable from a live one.

## Decision

Treat every raw reference as non-owning.

- Never bind a reference to a fresh allocation, and never `delete` the address
  of a reference.
- A `T&` parameter, member or return value means "someone else owns this, and
  guarantees it outlives this use". Nothing more.
- Where ownership must be expressed, use a resource handle by value; where a
  legacy owning raw handle is unavoidable, express it as a marked owning
  pointer, not as a reference.
- A reference member does not make its class an owner, and such a class must not
  release what the reference refers to in its destructor.

## Rationale

The rule matters less for the code it forbids — `*new` bound to a reference is
rare and obviously wrong — than for the guarantee it completes. Once both `T*`
and `T&` are known to be non-owning, every unadorned handle in the codebase is a
view, and ownership is visible wherever it exists because it is spelled with a
different type. A single exempt form would be enough to make that reasoning
unavailable.

## Consequences

### Positive

- Completes the "raw means non-owning" default, so no unadorned type in a
  signature can hide ownership.
- A reference parameter is unambiguously a borrow, which is what nearly all of
  them already were.
- Removes the `delete &r` idiom, which defeats every tool that reasons about
  ownership through pointers.

### Negative

- There is no annotation for an owning reference, so legacy code that has one
  has no way to declare it, unlike the `owner<T*>` route available for pointers.
  This is a genuine hole in the rule rather than a cost of following it.
- A non-owning reference says nothing about lifetime, so a dangling reference is
  as easy to produce as before and looks safer than a dangling pointer.
- The rule is close to vacuous in new code, which makes it easy to record and
  easy to forget it applies to members and return values too, where the mistake
  is much more plausible.

### Neutral

- The choice between a reference and a pointer for a non-owning parameter is
  decided elsewhere, by whether absence is representable.
- This record adds no enforcement of its own; it borrows the raw pointer rule's.

## Enforcement

- As for the raw pointer rule. In particular, warn on `delete` applied to the
  address of a reference, and on binding a reference to the result of an
  allocation.

## Related

- [0149](0149-r-3-a-raw-pointer-a-t-is-non-owning.md) — the rule this one
  mirrors, and the source of its enforcement.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the interface rule that names references explicitly.
- [0159](0159-r-20-use-unique-ptr-or-shared-ptr-to-represent-ownership.md) —
  what to use when ownership is what you meant.
- [0014](0014-p-8-do-not-leak-any-resources.md) — the leak this prevents.
