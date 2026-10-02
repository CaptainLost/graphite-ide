# 0056. C.32: If a class has a raw pointer (T*) or reference (T&), consider whether it might be owning

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

There is a lot of code that is non-specific about ownership. A raw pointer
member says that the object knows where something is; it says nothing about
whether the object is responsible for destroying it.

```cpp
class legacy_class
{
    foo* m_owning;   // Bad: change to unique_ptr<T> or owner<T*>
    bar* m_observer; // OK: keep
}
```

The two members are declared identically and mean opposite things. The only way
to determine ownership may be code analysis — reading the constructors to see
whether an allocation is stored there, and reading the destructor to see whether
anything is released.

Ownership should be clear in new code, and in refactored legacy code: an owning
pointer is a `std::unique_ptr`, a `std::shared_ptr` or a `gsl::owner<T*>`; a
non-owning pointer is a plain `T*` or a `gsl::not_null<T*>`; and a reference
never owns.

## Decision

For every raw pointer or reference member, decide whether it owns, and then say
so in its type.

- If the member owns, change it to `std::unique_ptr<T>` or `std::shared_ptr<T>`,
  or where a raw pointer must be retained for layout or ABI reasons, mark it
  `gsl::owner<T*>`.
- If the member does not own, leave it a raw pointer, and use
  `gsl::not_null<T*>` when it must never be null, per
  [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md).
- Never treat a reference member as owning. Prefer a pointer member to a
  reference member in any case, per
  [0050](0050-c-12-dont-make-data-members-const-or-references.md).
- Do not leave the question open. A raw `T*` member in new code is a statement
  that the class does not own the pointee, and the destructor must therefore not
  release it.
- Once a member is established as owning, the class needs a destructor and the
  rest of the rule of five, per
  [0057](0057-c-33-if-a-class-has-an-owning-pointer-member-define-a-destructor.md).

## Rationale

Every other resource rule depends on the answer to this question. Whether the
destructor must release the member, whether the copy constructor must
deep-copy, whether the class can be copied at all — all of it follows from
ownership, and none of it can be decided while ownership is unstated.

Stating it in the type rather than in a comment is what makes the answer
survive. A comment saying "we own this" is not consulted by the copy constructor
someone adds two years later; a `unique_ptr` member makes that copy constructor
fail to compile.

## Consequences

### Positive

- Ownership becomes a property of the declaration, readable without tracing
  constructors.
- The compiler enforces the answer: an owning member that is a `unique_ptr`
  cannot be copied by accident.
- The remaining lifecycle decisions follow mechanically from the answer.

### Negative

- Converting an owning raw pointer to a smart pointer is not always a local
  change: it can alter the class's copyability, its layout, and its ABI, and it
  can ripple into every caller that took the raw pointer out.
- `gsl::owner<T*>` is an annotation, not a mechanism — it changes nothing at run
  time and enforces nothing without a checker that understands it, so on its own
  it only moves the comment into the type name.
- The rule asks for a decision that may be genuinely unknown in inherited code,
  and the honest answer sometimes requires reading every caller.

### Neutral

- The guideline deliberately stops short of requiring all owning pointers to be
  smart pointers, because that would sometimes require non-trivial code changes
  and could affect ABIs.
- The rule is about deciding and recording ownership, not about which owning
  representation is chosen.

## Enforcement

- Look at the initialisation of raw pointer and reference members and see
  whether an allocation is used.

## Related

- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the same question asked of interfaces rather than of members.
- [0057](0057-c-33-if-a-class-has-an-owning-pointer-member-define-a-destructor.md)
  — what follows from an answer of "owning".
- [0055](0055-c-31-all-resources-acquired-by-a-class-must-be-released-by-the-classs-destructor.md)
  — which members the destructor is responsible for.
- [0031](0031-i-12-declare-a-pointer-that-must-not-be-null-as-not-null.md) — the
  other property a pointer member should state in its type.
