# 0047. C.9: Minimize exposure of members

Status: accepted
Date: 2026-08-10
Source: C++ Core Guidelines, section C (Classes and class hierarchies), transcribed 2026-08-10

## Context

Encapsulation and information hiding. Minimising the chance of unintended access
simplifies maintenance.

```cpp
template<typename T, typename U>
struct pair {
    T a;
    U b;
    // ...
};
```

Whatever is done in the elided part, an arbitrary user of a `pair` can
arbitrarily and independently change its `a` and its `b`. In a large code base,
it is not easy to find which code does what to those members. That might be
exactly what is wanted; but to enforce a relation among members, they must be
private and the relation must be enforced through constructors and member
functions:

```cpp
class Distance {
public:
    // ...
    double meters() const { return magnitude*unit; }
    void set_unit(double u)
    {
            // ... check that u is a factor of 10 ...
            // ... change magnitude appropriately ...
            unit = u;
    }
    // ...
private:
    double magnitude;
    double unit;    // 1 is meters, 1000 is kilometers, 0.001 is millimeters, etc.
};
```

If the set of direct users of a set of variables cannot easily be determined,
the type or usage of that set cannot easily be changed or improved. For public
and protected data, that is usually the case.

A class can provide two interfaces to its users: one for derived classes
(protected) and one for general users (public). A derived class might be allowed
to skip a run-time check because it has already guaranteed correctness:

```cpp
class Foo {
public:
    int bar(int x) { check(x); return do_bar(x); }
    // ...
protected:
    int do_bar(int x); // do some operation on the data
    // ...
private:
    // ... data ...
};

class Dir : public Foo {
    //...
    int mem(int x, int y)
    {
        /* ... do something ... */
        return do_bar(x + y); // OK: derived class can bypass check
    }
};

void user(Foo& x)
{
    int r1 = x.bar(1);      // OK, will check
    int r2 = x.do_bar(2);   // error: would bypass check
    // ...
}
```

Protected *data*, as opposed to a protected member function, remains a bad idea.
Prefer the order public members, then protected members, then private members.

## Decision

Expose the least that lets the type be used.

- Make data members private by default, and make one public only when the
  members are genuinely independent and the type is an aggregate, per
  [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md).
- Never make a data member protected. Use a protected member function where a
  derived class needs a narrower or cheaper path than the public one.
- Do not mix public and private data in one type; if any of the data is private,
  all of it should be.
- Declare sections in the order public, protected, private.
- Where a relation among members must hold, enforce it in the constructors and
  the member functions rather than documenting it and leaving the members
  reachable.

## Rationale

The cost of a public member is not the access, it is that the set of code
depending on it is unbounded and unfindable. A private member's users are the
members of one class, and that list is a page long; a public member's users are
everywhere, so the member's type, units and meaning are frozen the moment it
ships.

The `Distance` example is the concise argument: `magnitude` and `unit` are
meaningless separately, and any change to one without the other is a bug. There
is no way to express that with both of them public — only a convention that
every caller must independently observe.

## Consequences

### Positive

- The set of code that can violate a relation among members is bounded by the
  class.
- Representation choices — units, storage type, redundancy — stay changeable.
- Unintended writes become compile errors rather than latent bugs.

### Negative

- It generates accessors. A type whose members are genuinely independent still
  ends up with a getter and a setter per member if the rule is applied without
  judgement, which is more code for no protection at all.
- Access through a member function can be slower than a direct read, and forces
  a definition to be visible for inlining, which pushes implementation back into
  the header.
- Making everything private removes aggregate initialisation and designated
  initialisers, which are the most readable way to construct a plain data
  bundle.

### Neutral

- The rule is about how much is exposed, not about whether the type has an
  invariant.
- A protected member function is explicitly allowed as a second, narrower
  interface for derived classes.

## Enforcement

- Flag protected data members.
- Flag types that mix public and private data.

## Related

- [0041](0041-c-2-use-class-if-the-class-has-an-invariant-use-struct-if-the-data-members-can-vary-independently.md)
  — whether there is anything to protect.
- [0046](0046-c-8-use-class-rather-than-struct-if-any-member-is-non-public.md) —
  the keyword that follows from having non-public members.
- [0043](0043-c-4-make-a-function-a-member-only-if-it-needs-direct-access-to-the-representation-of-a-class.md)
  — minimising exposure to functions as well as to callers.
- [0021](0021-i-2-avoid-non-const-global-variables.md) — the same argument at
  program scope: mutable state with unbounded reach cannot be reasoned about.
