# 0052. C.21: If you define or =delete any copy, move, or destructor function, define or =delete them all

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The semantics of copy, move and destruction are closely related, so if one needs
to be declared, the odds are that the others need consideration too.

Declaring any copy, move or destructor function — even as `=default` or
`=delete` — suppresses the implicit declaration of a move constructor and a move
assignment operator. Declaring a move constructor or a move assignment operator,
even as `=default` or `=delete`, causes an implicitly generated copy constructor
or copy assignment operator to be defined as deleted. As soon as any of these is
declared, the others should all be declared, to avoid unwanted effects such as
turning all potential moves into more expensive copies, or making a class
move-only by accident.

```cpp
struct M2 {   // bad: incomplete set of copy/move/destructor operations
public:
    // ...
    // ... no copy or move operations ...
    ~M2() { delete[] rep; }
private:
    pair<int, int>* rep;  // zero-terminated set of pairs
};

void use()
{
    M2 x;
    M2 y;
    // ...
    x = y;   // the default assignment
    // ...
}
```

Given that special attention was needed for the destructor — here, to
deallocate — the likelihood that the implicitly defined copy and move assignment
operators are correct is low. Here they produce a double deletion.

This is known as "the rule of five". If a default implementation is wanted while
another operation is being defined, write `=default` to show that this is
intentional for that function; if a generated function is not wanted, suppress
it with `=delete`.

When a destructor needs to be declared just to make it virtual, it can be
defaulted:

```cpp
class AbstractBase {
public:
    virtual ~AbstractBase() = default;
    // ...
};
```

To prevent slicing, make the copy and move operations protected or `=delete`d,
and add a `clone`:

```cpp
class ClonableBase {
public:
    virtual unique_ptr<ClonableBase> clone() const;
    virtual ~ClonableBase() = default;
    CloneableBase() = default;
    ClonableBase(const ClonableBase&) = delete;
    ClonableBase& operator=(const ClonableBase&) = delete;
    ClonableBase(ClonableBase&&) = delete;
    ClonableBase& operator=(ClonableBase&&) = delete;
    // ... other constructors and functions ...
};
```

Defining only the move operations, or only the copy operations, would have the
same effect here, but stating the intent explicitly for each special member
makes it more obvious to the reader.

Compilers enforce much of this rule and ideally warn about any violation.
Relying on an implicitly generated copy operation in a class with a destructor
is deprecated.

Writing these functions is error-prone; note the argument types:

```cpp
class X {
public:
    // ...
    virtual ~X() = default;            // destructor (virtual if X is meant to be a base class)
    X(const X&) = default;             // copy constructor
    X& operator=(const X&) = default;  // copy assignment
    X(X&&) = default;                  // move constructor
    X& operator=(X&&) = default;       // move assignment
};
```

A minor mistake — a misspelling, a missing `const`, `&` where `&&` was meant, or
a special function left out — can lead to errors or warnings. To avoid the
tedium and the possibility of errors, prefer the rule of zero.

## Decision

Declare all five of the copy constructor, copy assignment, move constructor,
move assignment and destructor, or none of them.

- If any one of the five must be user-declared, write all five explicitly, each
  as a definition, `=default`, or `=delete`.
- Write `=default` where the compiler's version is wanted, so that the choice is
  visible rather than inferred from an absence.
- Write `=delete` where an operation must not exist, rather than relying on it
  being implicitly suppressed by the declaration of another.
- For a polymorphic base, default the virtual destructor and `=delete` or
  protect the copy and move operations, providing `clone` where deep copying is
  needed.
- Check the signatures against the list above, member by member, when writing
  them.
- Prefer not to reach this rule at all, per
  [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md).

## Rationale

The rule exists because the language's implicit-declaration rules are not
symmetric and not intuitive: declaring a destructor deletes nothing visibly but
silently stops moves from being generated, so a class that gained a destructor
last year may be performing copies everywhere it used to move, with no
diagnostic and no change at any call site.

The `M2` example is the sharper half. A destructor that frees something is
evidence that the member-wise copy is wrong, since two objects will free the
same thing; the very fact that made the destructor necessary is what makes the
generated copy a double free. Declaring all five forces that question to be
answered rather than defaulted.

## Consequences

### Positive

- The lifecycle of the type is stated in one block that can be read as a whole.
- Silent conversion of moves into copies stops happening.
- A double free arising from a generated copy alongside a hand-written
  destructor cannot slip through unexamined.

### Negative

- It is five lines of boilerplate on every class that needed one of them, and
  those five lines are exactly the ones that are easy to misdeclare — a missing
  `const` or the wrong reference kind produces a silently different function
  rather than an error.
- Declaring all five for the sake of the rule can suppress the compiler's
  ability to elide or optimise where a defaulted operation would have been
  trivial, and a `=default`ed operation declared out of line is not trivial.
- The rule is stated over five functions but the default constructor is outside
  it, so "all the special members" is still not the same as "all five", which is
  a recurring source of confusion.

### Neutral

- Compilers already diagnose much of this, so in a warnings-as-errors build the
  rule is partly enforced before any reviewer sees the code.
- The rule says the set must be complete, not what any member of it should do.

## Enforcement

- (Simple) A class should have a declaration — even a `=delete`d one — for
  either all or none of the copy, move and destructor functions.

## Related

- [0051](0051-c-20-if-you-can-avoid-defining-default-operations-do.md) — the
  preferred alternative to being subject to this record at all.
- [0053](0053-c-22-make-default-operations-consistent.md) — declaring all five
  is necessary but not sufficient; they must also agree.
- [0057](0057-c-33-if-a-class-has-an-owning-pointer-member-define-a-destructor.md)
  — the commonest reason a class enters this rule.
- [0058](0058-c-35-a-base-class-destructor-should-be-either-public-and-virtual-or-protected-and-non-virtual.md)
  — the other common reason, and the `ClonableBase` shape above.
