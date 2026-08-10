# 0070. C.49: Prefer initialization to assignment in constructors

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

An initialisation explicitly states that initialisation, rather than assignment,
is being done. It can be more elegant and more efficient, and it prevents
"use before set" errors.

```cpp
class A {   // Good
    string s1;
public:
    A(czstring p) : s1{p} { }    // GOOD: directly construct (and the C-string is explicitly named)
    // ...
};
```

Assigning in the body constructs the member twice, and in the worst case reads
it before it holds anything:

```cpp
class B {   // BAD
    string s1;
public:
    B(const char* p) { s1 = p; }   // BAD: default constructor followed by assignment
    // ...
};

class C {   // UGLY, aka very bad
    int* p;
public:
    C() { cout << *p; p = new int{10}; }   // accidental use before initialized
    // ...
};
```

Better still, the parameter type can carry its own extent instead of being a raw
pointer:

```cpp
class D {   // Good
    string s1;
public:
    D(string_view v) : s1{v} { }    // GOOD: directly construct
    // ...
};
```

## Decision

Give a member its value in the member initialiser list, not in the constructor
body.

- Initialise every member in the initialiser list, or from its in-class
  initialiser; reserve the constructor body for work that is not member
  initialisation.
- Never read a member in the constructor body before every member it depends on
  has been initialised.
- Prefer a parameter type that states what it is — `std::string_view` or a
  `span` rather than `const char*` — so that the member can be constructed
  directly from it.
- Where a member's value requires several statements to compute, compute it in a
  helper function and initialise the member from that function's result rather
  than default-constructing and assigning.

## Rationale

Assignment in the body is not a stylistic variant of initialisation; it is a
different sequence of operations. The member is default-constructed first and
then overwritten, so a type with an expensive default pays for it, and a type
with no default does not compile at all — which is at least an honest failure.

The `C` example is the reason the rule is stated as a prohibition rather than a
preference: once initialisation happens in the body, nothing distinguishes a
member that has been given a value from one that has not, and reading the wrong
one is undefined behaviour with no diagnostic.

## Consequences

### Positive

- Members with no default constructor, and `const` and reference members, work
  without special handling.
- The double construction disappears, which for a string or a container is a
  measurable cost.
- The window in which a member exists without a value shrinks to nothing.

### Negative

- A value that takes several statements to compute does not fit in an
  initialiser list, so it needs a helper function that exists only for that,
  which is more code than the assignment it replaced.
- Initialiser lists are subject to the declaration-order rule
  ([0068](0068-c-47-define-and-initialize-member-variables-in-the-order-of-member-declaration.md)),
  so moving work into them imports an ordering hazard the body did not have.
- Error handling is awkward in an initialiser list: there is no place to check a
  precondition and act on it, so the only way to fail from inside one is to
  throw, out of a context where the enclosing object's destructor will not run
  and where a function-try-block is the only handler available.

### Neutral

- Members that are merely written in the body as part of the object's real work,
  rather than being given their initial value, are not what this rule is about.
- The `string_view` improvement is a separate rule that happens to make this one
  easier to follow.

## Related

- [0069](0069-c-48-prefer-in-class-initializers-to-member-initializers-in-constructors-for-constant-initializers.md)
  — the constant case, where the initialiser moves further still, onto the
  declaration.
- [0068](0068-c-47-define-and-initialize-member-variables-in-the-order-of-member-declaration.md)
  — the ordering constraint that applies once everything is in the list.
- [0032](0032-i-13-do-not-pass-an-array-as-a-single-pointer.md) — why the `D`
  version's `string_view` parameter beats `const char*`.
- [0071](0071-c-50-use-a-factory-function-if-you-need-virtual-behavior-during-initialization.md)
  — where initialisation cannot be finished in the initialiser list.
