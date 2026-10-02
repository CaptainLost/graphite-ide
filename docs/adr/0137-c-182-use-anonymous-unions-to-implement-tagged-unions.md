# 0137. C.182: Use anonymous unions to implement tagged unions

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A well-designed tagged union is type safe. An anonymous union simplifies the
definition of a class with a tag-and-union pair, because the union's members are
named directly as members of the enclosing class rather than through an
intermediate object.

The example below is mostly borrowed from *The C++ Programming Language*, fourth
edition, pages 216 to 218. The code is somewhat elaborate: handling a type with
a user-defined assignment and destructor is tricky, and saving programmers from
having to write such code is one reason for including `variant` in the standard.

```cpp
class Value { // two alternative representations represented as a union
private:
    enum class Tag { number, text };
    Tag type; // discriminant

    union { // representation (note: anonymous union)
        int i;
        string s; // string has default constructor, copy operations, and destructor
    };

public:
    struct Bad_entry { }; // used for exceptions

    ~Value();
    Value& operator=(const Value&);   // necessary because of the string variant
    Value(const Value&);
    // ...
    int number() const;
    string text() const;

    void set_number(int n);
    void set_text(const string&);
    // ...
};

int Value::number() const
{
    if (type != Tag::number) throw Bad_entry{};
    return i;
}

string Value::text() const
{
    if (type != Tag::text) throw Bad_entry{};
    return s;
}

void Value::set_number(int n)
{
    if (type == Tag::text) {
        s.~string();      // explicitly destroy string
        type = Tag::number;
    }
    i = n;
}

void Value::set_text(const string& ss)
{
    if (type == Tag::text)
        s = ss;
    else {
        new(&s) string{ss};   // placement new: explicitly construct string
        type = Tag::text;
    }
}

Value& Value::operator=(const Value& e)   // necessary because of the string variant
{
    if (type == Tag::text && e.type == Tag::text) {
        s = e.s;    // usual string assignment
        return *this;
    }

    if (type == Tag::text) s.~string(); // explicit destroy

    switch (e.type) {
    case Tag::number:
        i = e.i;
        break;
    case Tag::text:
        new(&s) string(e.s);   // placement new: explicit construct
    }

    type = e.type;
    return *this;
}

Value::~Value()
{
    if (type == Tag::text) s.~string(); // explicit destroy
}
```

Note how the example handles a wrong-member access: it throws. That is one of
two defensible choices. The other is to state the tag check as a precondition on
the accessor and require the caller to have consulted the tag first, which is
cheaper and less safe.

## Decision

Where a tagged union must be written by hand, build it from an anonymous union
inside a class that owns the tag.

- Declare the union anonymously, as a private member alongside the discriminant,
  so the alternatives are named directly and no caller can reach the union
  itself.
- Give every accessor a tag check, so that reading the wrong alternative is a
  reported error and not undefined behaviour.
- Change the active member by explicitly destroying the old one and
  placement-constructing the new one, and update the tag as part of the same
  operation.
- Write the destructor, copy constructor, copy assignment and, where relevant,
  the move operations by hand; the compiler deletes them when a member has a
  non-trivial one.
- Handle self-assignment and the same-alternative case, as `operator=` above
  does, so an existing member is assigned rather than destroyed and rebuilt.
- Use `std::variant` in preference to all of this, and write it by hand only
  where layout or an external format demands it.

## Rationale

The point of the anonymous form is that the tag and the alternatives become
members of the same class, so no code can obtain the union without also
obtaining the tag that interprets it. That is what makes the invariant
enforceable rather than merely documented. The example is also the strongest
argument for not doing this at all: correctly changing the active member of a
union containing a `std::string` takes explicit destructor calls, placement new,
and four special member functions, every one of which is a chance to leak or to
destroy the wrong thing.

## Consequences

### Positive

- The tag and the alternatives cannot be separated, so the invariant has a
  single owner.
- Members are named directly, without an intermediate union object, so the class
  reads as if it had ordinary members.
- Every access goes through a checked accessor, converting undefined behaviour
  into a reported error.

### Negative

- The amount of hand-written code is large and every part of it is easy to get
  subtly wrong: a missed destructor call leaks, a missed placement new is
  undefined behaviour, and neither is diagnosed.
- Exception safety during the change of active member is delicate — if the
  placement construction throws after the old member has been destroyed, the
  object holds no valid alternative at all, which is precisely the
  valueless-by-exception state `std::variant` has to expose.
- Adding an alternative means revisiting the destructor, both copy operations,
  both move operations and every accessor, with no compiler assistance.
- The hand-written special members make the class non-trivial, so it loses
  trivial copyability and every optimisation that depends on it — which a plain
  union of trivial alternatives had for nothing.

### Neutral

- Upstream offers no enforcement, and none is plausible: the correctness of the
  construction lies in matching destructor and placement-new calls across
  several functions.
- The rule tells you how to build the wrapper, not whether the union was
  warranted; that is
  [0135](0135-c-180-use-unions-to-save-memory.md).

## Related

- [0136](0136-c-181-avoid-naked-unions.md) — this is the wrapper that rule
  demands.
- [0135](0135-c-180-use-unions-to-save-memory.md) — the justification a tagged
  union must have.
- [0017](0017-p-11-encapsulate-messy-constructs-rather-than-spreading-through-the-code.md)
  — placement new and explicit destructor calls belong inside one class.
