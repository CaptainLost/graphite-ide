# 0164. R.30: Take smart pointers as parameters only to explicitly express lifetime semantics

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section R (Resource management), transcribed 2026-08-10

## Context

R.30 carries no text of its own; upstream it refers to a rule in the section on
functions. Its substance is stated within this section, in the discussion of
non-standard smart pointers: a function should accept a smart pointer only if it
needs to participate in the object's lifetime management. Otherwise it should
accept a `widget*` if the argument can be null, and otherwise — ideally — a
`widget&`.

Upstream calls taking a smart pointer the function does not need a "silent
pessimisation", and leaves it at that. Two costs are behind the phrase, neither
spelled out there, and both are stated here as this record's own reading.

The runtime one is that a `shared_ptr` passed by value costs an atomic increment
on entry and an atomic decrement on exit, for a function whose body only calls
`p->foo()`. That cost is invisible at the call site and invisible in the body;
nothing about the code suggests that anything was paid.

The narrowing one is worse. A function taking `shared_ptr<widget>` cannot be
called with a widget that lives on the stack, or in a container, or is owned by
a `unique_ptr`, or is a member of another object. The parameter type has
restricted the callers to those who happen to own the object through one
particular mechanism, in exchange for nothing, because the function did not want
ownership.

The rules that follow this one are the vocabulary for the cases where a smart
pointer parameter is warranted: `unique_ptr<widget>` by value to assume
ownership, `unique_ptr<widget>&` to reseat, `shared_ptr<widget>` by value to
share ownership, `shared_ptr<widget>&` to reseat, and `const
shared_ptr<widget>&` to say that a reference count might be retained.

## Decision

Take a smart pointer parameter only when the function participates in the
object's lifetime.

- If the function only uses the object, take `widget&`; take `widget*` if the
  argument may legitimately be absent.
- Take `unique_ptr<widget>` by value to assume ownership, `unique_ptr<widget>&`
  to reseat the caller's pointer, `shared_ptr<widget>` by value to share
  ownership, `shared_ptr<widget>&` to reseat it, and `const shared_ptr<widget>&`
  only when a reference count might be retained on some path.
- Do not take `const unique_ptr<widget>&`: it expresses nothing about lifetime
  and merely refuses callers who own the widget some other way.
- Apply the same rule to member functions and constructors: a constructor that
  stores the object shares or takes ownership and should say so; one that merely
  reads it should take a reference.
- When calling down from a function that holds a smart pointer, pass `*p` or
  `p.get()`, not the smart pointer itself, subject to the aliasing rule.

## Rationale

A parameter type is a statement about what the function does with its argument,
and a smart pointer says "I take part in deciding when this dies". Saying that
when it is not true costs the caller both time and reach: refcount traffic on
every call, and an interface usable only by callers who own the object the same
way. A `widget&` says exactly what almost every function actually needs — the
object exists for the duration of the call — and is callable by every owner
regardless of scheme.

## Consequences

### Positive

- Interfaces become usable with stack objects, members and container elements,
  not only with heap objects held by a particular smart pointer.
- Refcount traffic disappears from functions that never needed it.
- The presence of a smart pointer in a signature regains its meaning, because it
  now occurs only where lifetime is genuinely at stake.

### Negative

- Raw references and pointers passed down a call chain rely on the object
  outliving the call, which nothing checks; the rule trades a refcount for a
  lifetime obligation, and the aliasing rule exists because that obligation is
  routinely violated.
- The vocabulary is fine-grained — five parameter forms with distinct meanings —
  so the rule only works if callers and readers know it, and a misread form
  silently means something else.
- It pushes toward interfaces taking `widget&`, which cannot express absence, so
  optional arguments must fall back to a pointer and the null check returns.
- The upstream rule delegates its argument to the functions section, so this
  record reconstructs it from the surrounding material rather than transcribing
  a statement of its own.

### Neutral

- The rule concerns parameters. What a function stores in a member is a separate
  decision, and storing does imply lifetime participation.
- Nothing here says which owner a caller should use, only what a callee should
  ask for.

## Related

- [0165](0165-r-31-if-you-have-non-std-smart-pointers-follow-the-basic-pattern-from-std.md)
  — where this rule's substance is stated upstream, and how it extends to
  third-party smart pointers.
- [0166](0166-r-32-take-a-unique-ptr-widget-parameter-to-express-that-a-function-assumes-ownership-of-a-widget.md)
  and
  [0168](0168-r-34-take-a-shared-ptr-widget-parameter-to-express-shared-ownership.md)
  — the cases where a smart pointer parameter is correct.
- [0148](0148-r-2-in-interfaces-use-raw-pointers-to-denote-individual-objects-only.md)
  — what a raw pointer parameter means once this rule is in force.
- [0171](0171-r-37-do-not-pass-a-pointer-or-reference-obtained-from-an-aliased-smart-pointer.md)
  — the safety condition on passing raw pointers down a call chain.
