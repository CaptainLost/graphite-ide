# 0081. C.67: A polymorphic class should suppress public copy/move

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

A polymorphic class is a class that defines or inherits at least one virtual
function. It is likely to be used as a base class for other derived classes with
polymorphic behaviour. If it is accidentally passed by value, with the
implicitly generated copy constructor and assignment, we risk slicing: only the
base portion of a derived object will be copied, and the polymorphic behaviour
will be corrupted.

If the class has no data, `=delete` the copy and move functions. Otherwise, make
them protected.

```cpp
class B { // BAD: polymorphic base class doesn't suppress copying
public:
    virtual char m() { return 'B'; }
    // ... nothing about copy operations, so uses default ...
};

class D : public B {
public:
    char m() override { return 'D'; }
    // ...
};

void f(B& b)
{
    auto b2 = b; // oops, slices the object; b2.m() will return 'B'
}

D d;
f(d);
```

Deleting the operations turns the silent corruption into a compile error:

```cpp
class B { // GOOD: polymorphic class suppresses copying
public:
    B() = default;
    B(const B&) = delete;
    B& operator=(const B&) = delete;
    virtual char m() { return 'B'; }
    // ...
};

class D : public B {
public:
    char m() override { return 'D'; }
    // ...
};

void f(B& b)
{
    auto b2 = b; // ok, compiler will detect inadvertent copying, and protest
}

D d;
f(d);
```

If you need to create deep copies of polymorphic objects, use a `clone()`
function. The exception is that classes representing exception objects need both
to be polymorphic and copy-constructible.

## Decision

Do not leave copy and move operations publicly available on a class that has
virtual functions.

- If the polymorphic class has no data members, `=delete` its copy constructor,
  copy assignment, move constructor, and move assignment.
- If it has data that derived classes need copied, declare those operations
  `protected` instead, so derived classes can use them and outside code cannot.
- Provide a virtual `clone()` returning a smart pointer where callers genuinely
  need to duplicate an object through a base reference.
- Do not pass or return a polymorphic type by value; pass by reference or by a
  smart pointer.
- Exception types are the exception: they must be polymorphic and
  copy-constructible, so they keep their public copy operations.

## Rationale

Slicing is not detectable at the point where it does harm. `auto b2 = b;`
compiles, runs, and produces an object of the right static type whose behaviour
is wrong — `b2.m()` returns `'B'` where the original returned `'D'` — and the
symptom appears wherever `b2` is next used. Deleting the operation moves the
whole failure to the line that caused it.

The choice between `delete` and `protected` is not stylistic: `protected` is
required whenever a derived class's own copy operations need to copy the base
subobject, and `delete` would make those derived operations impossible to write.

## Consequences

### Positive

- Accidental pass-by-value or assignment of a polymorphic object stops
  compiling, at the offending line.
- The hierarchy's copy semantics become an explicit design decision, expressed
  as `clone()` or as nothing.
- Derived classes retain a usable base copy through the `protected` form when
  they need one.

### Negative

- Deleting or protecting the copy operations makes the class non-assignable and
  non-copyable for legitimate uses too, so any code that stored such objects by
  value must be restructured around references or smart pointers.
- `clone()` allocates, returns an owning pointer, and has to be overridden in
  every derived class; forgetting one override yields a base-typed clone, which
  is the slicing bug back again by another route.
- Declaring these operations at all suppresses others by the language's rules,
  so the class ends up spelling out the full set of special members whether or
  not it wanted to.

### Neutral

- Exception types are explicitly exempt: the language copies a thrown object, so
  an exception class must remain both polymorphic and copy-constructible.
- The rule constrains the accessibility of copy and move, not whether the
  hierarchy should exist; a class without virtual functions is untouched by it.

## Enforcement

- Flag a polymorphic class with a public copy operation.
- Flag an assignment of polymorphic class objects.

## Related

- [0075](0075-c-61-a-copy-operation-should-copy.md) — a sliced copy is precisely
  a copy that does not copy.
- [0083](0083-c-81-use-delete-when-you-want-to-disable-default-behavior-without-wanting-an-alternative.md)
  — the mechanism this record calls for.
- [0036](0036-i-25-prefer-empty-abstract-classes-as-interfaces-to-class-hierarchies.md)
  — the data-free interface class, where `=delete` is the right form.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  what `clone()` should return.
