# 0071. C.50: Use a factory function if you need "virtual behavior" during initialization

Status: proposed
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

If the state of a base class object must depend on the state of a derived part
of the object, a virtual function or equivalent is needed — while minimising the
window of opportunity to misuse an imperfectly constructed object.

Calling the virtual function from the constructor does not do what it looks like
it does:

```cpp
class B {
public:
    B()
    {
        /* ... */
        f(); // BAD: C.82: Don't call virtual functions in constructors and destructors
        /* ... */
    }
    virtual void f() = 0;
};
```

The fix is to make the constructor unreachable from outside and run the virtual
step immediately after construction, from a factory:

```cpp
class B {
protected:
    class Token {};
public:
    explicit B(Token) { /* ... */ }  // create an imperfectly initialized object
    virtual void f() = 0;
    template<class T>
    static shared_ptr<T> create()    // interface for creating shared objects
    {
        auto p = make_shared<T>(typename T::Token{});
        p->post_initialize();
        return p;
    }
protected:
    virtual void post_initialize()   // called right after construction
        { /* ... */ f(); /* ... */ } // GOOD: virtual dispatch is safe
};

class D : public B {                 // some derived class
protected:
    class Token {};
public:
    explicit D(Token) : B{ B::Token{} } {}
    void f() override { /* ...  */ };
protected:
    template<class T>
    friend shared_ptr<T> B::create();
};

shared_ptr<D> p = D::create<D>();  // creating a D object
```

`make_shared` requires that the constructor be public. By requiring a protected
`Token` the constructor cannot be called publicly any more, so an incompletely
constructed object cannot escape into the wild. Providing the factory function
`create()` makes construction on the free store convenient.

The return type of the factory should normally be `unique_ptr` by default; if
some uses are shared, the caller can move the `unique_ptr` into a `shared_ptr`.
However, if the factory author knows that all uses of the returned object will
be shared uses, return `shared_ptr` and use `make_shared` in the body to save an
allocation. Conventional factory functions allocate on the free store rather
than on the stack or in an enclosing object.

## Decision

When initialisation must dispatch to a derived override, put the dispatch in a
factory function that runs after the object is fully constructed.

- Do not call a virtual function from a constructor in order to reach a derived
  override; the override will not be reached.
- Make the constructor unusable from outside — a protected tag type such as
  `Token` is the idiom that still permits `make_shared` — and expose a static
  `create()` instead.
- Have `create()` construct the object, call a protected virtual
  `post_initialize()`, and only then hand the object to the caller.
- Return `unique_ptr` unless every use is known to be shared, in which case
  return `shared_ptr` built by `make_shared`.

## Rationale

During a base constructor the object's dynamic type is the base, so the virtual
call resolves to the base — this is not a compiler limitation but a
deliberate rule, because the derived members do not exist yet. There is
therefore no way to get derived behaviour into base construction; the only
question is where the derived step runs instead.

The factory answers that question in the one place where the derived object is
complete and no caller has yet seen it. The `Token` is what makes it a rule
rather than a suggestion: without it, someone can still call the constructor
directly and skip `post_initialize()`.

## Consequences

### Positive

- The virtual call happens against the complete object, so the override runs.
- The partially initialised state cannot be observed by a caller, because no
  caller can construct the object.
- Where construction can fail, the factory has a return value to report the
  failure through, which a constructor does not.

### Negative

- The object is forced onto the free store: a type built this way cannot be a
  stack local or a by-value member, which is a large design constraint imposed
  by an initialisation detail.
- Every derived class must repeat the `Token` boilerplate and befriend
  `create()`, and a derived class that forgets it silently reopens the hole.
- There is still a window — between the constructor returning and
  `post_initialize()` finishing — during which the object exists in a
  half-built state, so the invariant is enforced by the factory's discipline
  rather than by the type.

### Neutral

- The rule does not say a two-phase construction is good, only that if you need
  one it belongs behind a factory rather than in a constructor.
- Choosing `unique_ptr` or `shared_ptr` is an ownership decision governed by
  [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md).

## Related

- [0084](0084-c-82-dont-call-virtual-functions-in-constructors-and-destructors.md)
  — the prohibition this record gives the workaround for.
- [0030](0030-i-11-never-transfer-ownership-by-a-raw-pointer-or-reference.md) —
  the factory returns a smart pointer for the reason that record gives.
- [0067](0067-c-46-by-default-declare-single-argument-constructors-explicit.md) —
  the `Token` constructor is single-argument and correctly `explicit`.
