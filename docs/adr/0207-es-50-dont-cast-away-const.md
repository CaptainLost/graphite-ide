# 0207. ES.50: Don't cast away const

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section ES (Expressions and statements), transcribed 2026-08-10

## Context

Casting away `const` makes a lie out of `const`. If the variable is actually
declared `const`, modifying it is undefined behaviour.

```cpp
void f(const int& x)
{
    const_cast<int&>(x) = 42;   // BAD
}

static int i = 0;
static const int j = 0;

f(i); // silent side effect
f(j); // undefined behavior
```

The two calls differ only in the declaration of the argument, and the function
cannot tell them apart.

A common temptation is to reach for `const_cast` to avoid duplicating logic,
when two accessors differ only in constness:

```cpp
class Bar;

class Foo {
public:
    // BAD, duplicates logic
    Bar& get_bar()
    {
        /* complex logic around getting a non-const reference to my_bar */
    }

    const Bar& get_bar() const
    {
        /* same complex logic around getting a const reference to my_bar */
    }
private:
    Bar my_bar;
};
```

Normally the non-const function can just call the const one, but with complex
logic that leads to a pattern which still resorts to a cast:

```cpp
class Foo {
public:
    // not great, non-const calls const version but resorts to const_cast
    Bar& get_bar()
    {
        return const_cast<Bar&>(static_cast<const Foo&>(*this).get_bar());
    }

    const Bar& get_bar() const
    {
        /* the complex logic around getting a const reference to my_bar */
    }
private:
    Bar my_bar;
};
```

That pattern is safe when applied correctly, because the caller must have had a
non-const object to begin with, but it is not ideal: the safety is hard to
enforce automatically as a checker rule. Putting the common code in a helper
template that deduces constness uses no cast at all:

```cpp
class Foo {
public:                         // good
          Bar& get_bar()       { return get_bar_impl(*this); }
    const Bar& get_bar() const { return get_bar_impl(*this); }
private:
    Bar my_bar;

    template<class T>           // good, deduces whether T is const or non-const
    static auto& get_bar_impl(T& t)
        { /* the complex logic around getting a possibly-const reference to my_bar */ }
};
```

Do not do large non-dependent work inside such a template, which leads to code
bloat; factoring the non-dependent part into a common non-template function can
give a big reduction in code size.

There is an exception: you might need to cast away `const` when calling
const-incorrect functions. Prefer to wrap such functions in inline const-correct
wrappers, so the cast is encapsulated in one place.

A second temptation is updating transient information — caching, memoisation,
precomputation — inside a logically constant operation:

```cpp
int compute(int x); // compute a value for x; assume this to be costly

class Cache {   // some type implementing a cache for an int->int operation
public:
    pair<bool, int> find(int x) const;   // is there a value for x?
    void set(int x, int v);             // make y the value for x
    // ...
};

class X {   // Suspicious solution based on casting
public:
    int get_val(int x) const
    {
        auto p = cache.find(x);
        if (p.first) return p.second;
        int val = compute(x);
        const_cast<Cache&>(cache).set(x, val);   // ugly
        return val;
    }
    // ...
private:
    Cache cache;
};
```

The better solution states that the cache is mutable even for a const object:

```cpp
class X {   // better solution
public:
    int get_val(int x) const
    {
        auto p = cache.find(x);
        if (p.first) return p.second;
        int val = compute(x);
        cache.set(x, val);
        return val;
    }
    // ...
private:
    mutable Cache cache;
};
```

An alternative is to hold the cache behind an indirection, such as a
`unique_ptr<Cache>`. That is the most flexible solution but requires explicit
construction and destruction of the cache. In any variant, guard against data
races on the cache in multi-threaded code, possibly with a `std::mutex`.

## Decision

Never remove `const` from an object in order to modify it.

- Do not write `const_cast` to enable a write. If the object was declared
  `const`, the write is undefined behaviour, and the code that decides this is
  usually not the code that declared it.
- Do not use `const_cast` to share implementation between a const and a
  non-const accessor. Factor the shared logic into a static member template
  that deduces constness, and have both accessors call it.
- Where a member must change while the object is logically unchanged — a cache,
  a memo, a lazily computed field — declare that member `mutable`, or hold it
  behind an indirection.
- Where a third-party API is const-incorrect, put the `const_cast` in one inline
  wrapper with a const-correct signature and call the wrapper everywhere else.
- Where a `mutable` member is written from a `const` member function, say what
  the thread-safety contract is, and enforce it; a `const` member function that
  mutates state is no longer trivially safe to call concurrently.

## Rationale

The two-call example is the argument that cannot be answered locally. `f(i)` has
a silent side effect and `f(j)` is undefined, and the body of `f` is identical
in both cases — so the correctness of a `const_cast` is never decidable from the
code containing it. It depends on how every caller declared its object, now and
in every future revision.

`mutable` and the deducing helper are not merely tidier spellings of the same
thing. They move the claim to where it can be checked: `mutable` is a statement
by the class about its own representation, and the helper template proves the
non-const path never touches a `const` object because the compiler instantiated
it that way.

## Consequences

### Positive

- `const` continues to mean something, which is what allows callers, optimisers
  and reviewers to reason about which functions can change what.
- The duplication problem the cast was solving is solved better, with the
  const/non-const distinction handled by deduction rather than by assertion.
- Logically-const mutation acquires a declared home in the class, so a reader
  can see which members are outside the object's observable value.

### Negative

- The deducing helper must live in the header and is instantiated twice, so it
  trades a cast for template code bloat. The source acknowledges this and
  recommends factoring non-dependent work back out, which is more work again.
- `mutable` weakens the guarantee that a `const` member function is safe to call
  from several threads, and nothing in the type system flags that. The cast made
  the danger visible; `mutable` hides it in the class definition.
- Const-incorrect third-party APIs are common, so the exception is not rare, and
  every wrapper written for one is code that must be maintained alongside the
  dependency.
- Flagging all `const_cast` will fire on the legitimate wrapper cases, so the
  enforcement is a review prompt rather than a rule that can be left on as an
  error.

### Neutral

- Casting away `const` and then only reading is well defined, but it is
  pointless: the reason to write it was to remove a diagnostic that was correct.
- Whether the cache is `mutable` or held behind a `unique_ptr` is a separate
  design choice; both satisfy this record.

## Enforcement

- Flag `const_cast`.
- This rule is part of the type-safety profile.

## Related

- [0016](0016-p-10-prefer-immutable-data-to-mutable-data.md) — the general
  preference this rule protects.
- [0205](0205-es-48-avoid-casts.md) — casting away `const` is the worst case of
  the general problem.
- [0206](0206-es-49-if-you-must-use-a-cast-use-a-named-cast.md) — `const_cast`
  is a named cast, and being named does not make it safe.
- [0203](0203-es-46-avoid-lossy-narrowing-truncating-arithmetic-conversions.md)
  — the other conversion rule enforced by the same type-safety profile.
