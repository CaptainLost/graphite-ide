# 0105. C.130: For making deep copies of polymorphic classes prefer a virtual clone function instead of public copy construction/assignment

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Copying a polymorphic class is discouraged because of the slicing problem: a
copy made through a reference or pointer to base copies only the base
sub-object, and the result is an object of the base type that has silently lost
everything the derived class added. If copy semantics are really needed, copy
deeply. Provide a virtual `clone` function that copies the actual most-derived
type and returns an owning pointer to the new object, and in derived classes
return the derived type, using a covariant return type.

```cpp
class B {
public:
    B() = default;
    virtual ~B() = default;
    virtual gsl::owner<B*> clone() const = 0;
protected:
     B(const B&) = default;
     B& operator=(const B&) = default;
     B(B&&) = default;
     B& operator=(B&&) = default;
    // ...
};

class D : public B {
public:
    gsl::owner<D*> clone() const override
    {
        return new D{*this};
    };
};
```

Note the shape of the base. The copy and move operations are not deleted, they
are made `protected` and defaulted: a derived class's own copy constructor still
needs them to copy the base sub-object, so `D`'s `clone` can write
`new D{*this}`, but no outside caller can slice a `B` out of a `D`.

Generally it is recommended to use smart pointers to represent ownership.
However, because of the language rules the covariant return type cannot be a
smart pointer: `D::clone` cannot return `unique_ptr<D>` while `B::clone` returns
`unique_ptr<B>`. The choice is therefore either to return `unique_ptr<B>`
consistently in all overrides, giving up covariance, or to use the `owner<>`
utility from the Guidelines Support Library as above.

## Decision

Copy a polymorphic object through a virtual `clone`, not through its copy
operations.

- Declare `virtual clone() const` on the polymorphic base, pure virtual where
  the base is abstract, and override it in every derived class.
- Implement each override as a copy of the most-derived type — `new D{*this}` —
  so no state is lost.
- Make the base's copy constructor, copy assignment, move constructor and move
  assignment `protected` and `= default`, so derived classes can use them and
  outside code cannot slice.
- Choose one of the two return conventions deliberately: `gsl::owner<D*>` with
  covariant returns, or `std::unique_ptr<B>` uniformly without them, and apply
  it consistently across the hierarchy.
- Where a raw owning pointer is returned, wrap it in a smart pointer at the call
  site immediately.

## Rationale

Slicing is the failure this rule exists for, and it has the property that
makes a rule worth writing: it is legal, silent, and produces a
plausible-looking object.
`B b = d;` compiles, runs, and yields something that answers virtual calls as a
`B`. No diagnostic appears anywhere.

Making the base's copy operations protected rather than deleted is the detail
that makes the pattern work rather than merely forbid. Deleting them would break
every derived class's own copy constructor; protecting them keeps copying
available exactly where it is correct — inside the hierarchy, on the full
object — and unavailable everywhere else.

## Consequences

### Positive

- A copy of a polymorphic object is a copy of the whole object, whatever its
  dynamic type.
- Slicing through a base reference or pointer becomes a compile error rather
  than a silent truncation.
- `clone` states in the interface that the type is copyable and how, which
  public copy operations on a polymorphic base do not.

### Negative

- Every derived class must implement `clone`, and a class that forgets to
  override it inherits a version that copies the wrong type — a mistake the
  compiler does not catch when the base's `clone` is not pure virtual.
- The covariant return type cannot be a smart pointer, so following the rule
  literally means returning a raw owning pointer and relying on `gsl::owner` and
  discipline, which sits badly with
  [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md).
- Cloning always allocates, so a value that could have been copied on the stack
  now costs a heap allocation and an indirection.
- Assignment has no equivalent: `clone` produces a new object, and there is no
  clean polymorphic counterpart to `a = b`.

### Neutral

- The alternative of returning `unique_ptr<B>` from every override is explicitly
  permitted and trades covariance for ownership safety; which trade is right
  depends on whether callers need the derived static type of the clone.
- The rule does not require a polymorphic class to be copyable; the first
  recommendation is not to copy one at all.

## Related

- [0102](0102-c-127-a-class-with-a-virtual-function-should-have-a-virtual-or-protected-destructor.md)
  — the same protected-versus-public choice for the destructor.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the rule the `gsl::owner<B*>` return value is in tension with.
- [0091](0091-c-90-rely-on-constructors-and-assignment-operators-not-memset-and-memcpy.md)
  — the other way a polymorphic object gets copied wrongly.
- [0099](0099-c-121-if-a-base-class-is-used-as-an-interface-make-it-a-pure-abstract-class.md)
  — the base being cloned is normally an interface.
