# 0461. F.42: Return a T* to indicate a position (only)

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section F (Functions), transcribed 2026-08-10

## Context

Indicating a position is what pointers are good for. Returning a `T*` to
transfer ownership is a misuse of them.

```cpp
Node* find(Node* t, const string& s)  // find s in a binary tree of Nodes
{
    if (!t || t->name == s) return t;
    if (auto p = find(t->left, s)) return p;
    if (auto p = find(t->right, s)) return p;
    return nullptr;
}
```

If it is not the `nullptr`, the pointer returned by `find` indicates a `Node`
holding `s`. Importantly, that does not imply a transfer of ownership of the
pointed-to object to the caller. The tree still owns its nodes; the caller has
been told where one of them is.

Positions can also be transferred by iterators, indices, and references. A
reference is often a superior alternative to a pointer, when there is no need to
use `nullptr`, or when the object referred to should not change.

The position returned must be a position in something that outlives the call.
Do not return a pointer to something that is not in the caller's scope; that is
the subject of
[0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md).

## Decision

Give a returned raw `T*` exactly one meaning: it locates an object that
something else owns.

- Return a raw `T*` to say "here it is, or nowhere" — a search result, a lookup
  in a container, a node in a structure the callee still owns.
- Return an owning type when the caller is to take ownership: `unique_ptr<T>`
  per
  [0459](0459-f-26-use-a-unique-ptr-t-to-transfer-ownership-where-a-pointer-is-needed.md),
  or `shared_ptr<T>` where sharing is real. Never a bare `T*`.
- Return `T&` instead when the result can never be absent, per
  [0463](0463-f-44-return-a-t-when-copy-is-undesirable-and-returning-no-object-isnt-needed.md),
  and `const T&` where the caller must not modify.
- Consider an iterator or an index instead where the container is the natural
  frame of reference, or where the position must survive reallocation.
- Never `delete` or `free` through a raw `T*` obtained this way, and never
  assign the result of `new` or `malloc` to a plain `T*`.

## Rationale

The value of the rule is that it makes the absence of a convention into a
convention. If a raw `T*` return always means position, then it never needs a
comment, and the reader of a call site never has to open the callee to find out
whether a `delete` is owed.

`find` shows why the distinction has to be carried in the type rather than in
the name: the function is well named, correct, and gives the caller no way to
tell from the signature that the tree still owns the node. Only the repository
convention that a returned `T*` is never an owner supplies that.

## Consequences

### Positive

- `delete` on a returned pointer becomes categorically wrong, which is a rule a
  reviewer or a tool can apply without knowing the function.
- Owning returns are forced into owning types, so leaks and double frees have
  fewer places to hide.
- `nullptr` acquires a single meaning at these boundaries: not found, rather
  than failed or not owned.

### Negative

- The convention is unenforced by the language. A `T*` return that does own
  something still compiles, and only reads wrong.
- It says where the object is and nothing about how long it stays there; the
  returned position can be invalidated by the next mutation of the container,
  and the type cannot express that.
- Using `nullptr` as "not found" means the result cannot be dereferenced without
  a check, and forgetting the check is undiagnosed.
- Returning an iterator instead is often better but is not a drop-in
  replacement: iterators need the container to be named in the signature, which
  couples the interface to the representation.

### Neutral

- References, iterators, and indices are all admitted alternatives; the rule
  restricts what a raw pointer means, not which of the four to pick.
- A static analyser tracking positions can eliminate much of the dangling risk,
  but this record does not assume one is configured here.

## Enforcement

- Flag `delete`, `std::free()`, and similar applied to a plain `T*`. Only owners
  should be deleted.
- Flag `new`, `malloc()`, and similar assigned to a plain `T*`. Only owners
  should be responsible for deletion.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the interface-level statement of the same prohibition.
- [0459](0459-f-26-use-a-unique-ptr-t-to-transfer-ownership-where-a-pointer-is-needed.md)
  — what to return instead when ownership does move.
- [0462](0462-f-43-never-directly-or-indirectly-return-a-pointer-or-a-reference-to-a-local-object.md)
  — the position must be in something that outlives the call.
- [0463](0463-f-44-return-a-t-when-copy-is-undesirable-and-returning-no-object-isnt-needed.md)
  — return a reference when "no object" is not a possible answer.
