# 0082. C.80: Use =default if you have to be explicit about using the default semantics

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

The compiler is more likely to get the default semantics right, and you cannot
implement these functions better than the compiler can.

```cpp
class Tracer {
    string message;
public:
    Tracer(const string& m) : message{m} { cerr << "entering " << message << '\n'; }
    ~Tracer() { cerr << "exiting " << message << '\n'; }

    Tracer(const Tracer&) = default;
    Tracer& operator=(const Tracer&) = default;
    Tracer(Tracer&&) = default;
    Tracer& operator=(Tracer&&) = default;
};
```

Because the destructor is defined, the copy and move operations must be defined
too. `= default` is the best and simplest way of doing that.

Writing the bodies out gives the same behaviour, more slowly and less reliably:

```cpp
class Tracer2 {
    string message;
public:
    Tracer2(const string& m) : message{m} { cerr << "entering " << message << '\n'; }
    ~Tracer2() { cerr << "exiting " << message << '\n'; }

    Tracer2(const Tracer2& a) : message{a.message} {}
    Tracer2& operator=(const Tracer2& a) { message = a.message; return *this; }
    Tracer2(Tracer2&& a) :message{a.message} {}
    Tracer2& operator=(Tracer2&& a) { message = a.message; return *this; }
};
```

Writing out the bodies of the copy and move operations is verbose, tedious, and
error-prone. A compiler does it better. Note that `Tracer2`'s hand-written move
operations copy `message` rather than moving it, which is exactly the kind of
mistake the rule is about.

## Decision

When a special member function must be declared but should behave as the
compiler would make it behave, declare it `= default`.

- Use `= default` for a default constructor, destructor, copy and move
  constructors, and copy and move assignment operators whose behaviour is
  member-wise.
- Declare them on the first declaration, inside the class, so that the type's
  triviality and `noexcept` properties are derived correctly.
- Do not write out a body that does what the compiler would have done; the body
  is not merely redundant, it is a place for a bug like `Tracer2`'s copying
  move.
- When a destructor is declared for any reason, declare the full set of copy and
  move operations too — `= default` where the default is right, `= delete` where
  it is not.

## Rationale

A defaulted function is not just shorter than the equivalent body; it is a
different thing to the language. The compiler derives its `noexcept`
specification, its triviality, and its `constexpr`-ness from the members, and
recomputes all of that when a member is added. A hand-written body freezes those
properties at whatever the author happened to produce, and adding a member to
the class silently leaves it out.

## Consequences

### Positive

- The operation stays correct when a member is added or its type changes.
- Triviality and `noexcept` are computed rather than asserted, so the type
  keeps whatever optimisations it is entitled to.
- Whole categories of hand-written mistakes — a forgotten member, a move that
  copies, a missing `return *this` — cannot occur.

### Negative

- `= default` inside a header exposes the members' operations to every
  translation unit, so it works against the compilation-firewall techniques a
  hand-written out-of-line body enables.
- Defaulting a function out of line in the `.cpp` behaves differently from
  defaulting it in the class — it is not trivial — and the distinction is subtle
  enough to be got wrong.
- Following the rule about declaring the whole set means five or six extra
  declarations on classes that previously had none, which is real clutter for a
  class whose only sin was needing a destructor.

### Neutral

- The rule says nothing about which operations a class should have, only how to
  spell the ones that should be default.
- Its counterpart for operations that should not exist is
  [0083](0083-c-81-use-delete-when-you-want-to-disable-default-behavior-without-wanting-an-alternative.md).

## Enforcement

- (Moderate) The body of a special operation should not have the same
  accessibility and semantics as the compiler-generated version, because that
  would be redundant.

## Related

- [0083](0083-c-81-use-delete-when-you-want-to-disable-default-behavior-without-wanting-an-alternative.md)
  — the same explicitness, for operations that should not be available.
- [0066](0066-c-45-dont-define-a-default-constructor-that-only-initializes-data-members-use-in-class-member-initializers-instead.md)
  — `= default` plus in-class initialisers replaces a hand-written default
  constructor.
- [0080](0080-c-66-make-move-operations-noexcept.md) — a defaulted move gets its
  `noexcept` from its members automatically.
- [0018](0018-p-12-use-supporting-tools-as-appropriate.md) — the compiler is the
  tool being deferred to here.
