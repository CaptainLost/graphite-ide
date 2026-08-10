# 0043. C.4: Make a function a member only if it needs direct access to the representation of a class

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A non-member function is less coupled than a member function. Fewer functions
can cause trouble by modifying object state, and fewer functions need to be
modified after a change in representation.

```cpp
class Date {
    // ... relatively small interface ...
};

// helper functions:
Date next_weekday(Date);
bool operator==(Date, Date);
```

The helper functions have no need for direct access to the representation of a
`Date`, so they are not members. They are still part of the useful interface to
the type; they are simply not part of the set of functions that can break its
invariant.

There are exceptions. The language requires virtual functions to be members, and
not all virtual functions directly access data — members of an abstract class
rarely do. The language also requires `=`, `()`, `[]` and `->` to be members.

An overload set may contain some members that do not directly access private
data:

```cpp
class Foobar {
public:
    void foo(long x) { /* manipulate private data */ }
    void foo(double x) { foo(std::lround(x)); }
    // ...
private:
    // ...
};
```

Similarly, a set of functions may be designed to be used in a chain:

```cpp
x.scale(0.5).rotate(45).set_color(Color::red);
```

Typically some but not all of such functions directly access private data, and
splitting the set would make the chain unusable.

## Decision

Default to a non-member, non-friend function, and promote it to a member only
when it must reach the representation.

- Write an operation as a free function in the class's namespace unless it needs
  to read or write private members.
- Keep it a member where the language requires one: virtual functions, and the
  `=`, `()`, `[]` and `->` operators.
- Keep the whole of an overload set together when at least one overload needs
  access, rather than splitting it across member and non-member forms, because
  splitting an overload set changes which overloads are found.
- Keep a chaining set together for the same reason, even where individual links
  in the chain touch nothing private.
- Put the resulting free functions in the class's namespace, per
  [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md).

## Rationale

The measure of a class's fragility is the number of functions that can violate
its invariant, and that number is exactly the number of members and friends. A
free function written against the public interface cannot corrupt the object no
matter how wrong it is, so moving an operation out of the class removes it from
the set of code that has to be re-examined when the representation changes.

The upstream note that the rule "becomes even better if C++ gets uniform
function call" is honest about the cost: the reason people write members that do
not need to be members is that `x.f()` reads better than `f(x)` and is easier to
discover. That is a real ergonomic loss, not a mistaken preference.

## Consequences

### Positive

- The set of code that can break the invariant stays small and enumerable.
- A change of representation touches fewer functions.
- The public interface is exercised by its own helpers, which tends to reveal
  when it is inadequate.

### Negative

- Free functions lose member call syntax and are harder to discover: they do not
  appear in an IDE's completion list after a dot, which is how most readers find
  out what a type can do.
- The rule pushes work through the public interface, which can be slower than
  direct member access and sometimes forces an accessor to be added that exists
  only to serve the helper.
- The exceptions are broad enough — virtual functions, four operators, whole
  overload sets, chaining sets — that a large fraction of real member functions
  are exempt, which weakens the rule in practice.

### Neutral

- The rule is about membership, not about visibility; a free helper is still
  part of the type's documented interface.
- Making a non-member a `friend` to give it access defeats the purpose and is
  not what this record permits.

## Enforcement

- Look for non-virtual member functions that do not touch data members directly.
  The snag is that many member functions that do not need to touch data members
  directly do.
- Ignore virtual functions.
- Ignore functions that are part of an overload set out of which at least one
  function accesses private members.
- Ignore functions returning `this`.

## Related

- [0044](0044-c-5-place-helper-functions-in-the-same-namespace-as-the-class-they-support.md)
  — where the functions this record excludes from the class should go.
- [0047](0047-c-9-minimize-exposure-of-members.md) — the same instinct applied
  to data rather than to functions.
- [0042](0042-c-3-represent-the-distinction-between-an-interface-and-an-implementation-using-a-class.md)
  — a helper that works through the public interface is a user of the boundary
  this record keeps intact.
